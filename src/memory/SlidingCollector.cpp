#include "SlidingCollector.h"

#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <string.h>

#ifndef FRUCTUS
  #include "../misc/Timer.h"
#endif
#include "../misc/debug.h"
#include "../misc/defs.h"  // NOLINT(misc-include-cleaner)
#include "../vm/IsValidObject.h"
#include "../vm/Print.h"
#include "../vm/Universe.h"
#ifdef FRUCTUS
  #include "../vm/FructusImage.h"
#endif
#include "../vmobjects/AbstractObject.h"
#include "../vmobjects/ObjectFormats.h"
#include "Heap.h"
#include "SlidingHeap.h"

using namespace SlidingLayout;

// The collection in progress. The mark stack is the live counts' table,
// which isn't used until the marking is done.
static SlidingHeap* theHeap;
static AbstractVMObject** markStack;
static size_t markStackSize;
static size_t markStackLimit;
static bool markStackOverflowed;
// The objects being collected, [low, high): young space for a minor
// collection, and the whole heap for a major one. The survivors slide up to
// high.
static char* low;
static char* high;

static bool isCollected(const void* p) {
    return p >= (const void*)low && p < (const void*)high;
}

static size_t popcount(MarkWord word) {
    if constexpr (sizeof(MarkWord) == sizeof(unsigned)) {
        return (size_t)__builtin_popcount(word);
    } else {
        return (size_t)__builtin_popcountl(word);
    }
}

static size_t clz(MarkWord word) {
    if constexpr (sizeof(MarkWord) == sizeof(unsigned)) {
        return (size_t)__builtin_clz(word);
    } else {
        return (size_t)__builtin_clzl(word);
    }
}

static size_t objectSize(const AbstractVMObject* obj) {
    return AllocationSize(obj->GetObjectSize());
}

static bool isMarked(const MarkWord* bits, size_t granule) {
    return ((bits[granule / MARK_BITS] >> (granule % MARK_BITS)) & 1U) != 0;
}

/** Sets the mark bits of `count` granules from `granule` on. */
static void markGranules(MarkWord* bits, size_t granule, size_t count) {
    size_t const last = granule + count - 1;
    size_t word = granule / MARK_BITS;
    size_t const lastWord = last / MARK_BITS;
    MarkWord const firstMask = ~(MarkWord)0 << (granule % MARK_BITS);
    MarkWord const lastMask =
        ~(MarkWord)0 >> (MARK_BITS - 1 - last % MARK_BITS);
    if (word == lastWord) {
        bits[word] |= firstMask & lastMask;
        return;
    }
    // the words in between are the object's alone
    bits[word] |= firstMask;
    for (word++; word < lastWord; word++) {
        bits[word] = ~(MarkWord)0;
    }
    bits[lastWord] |= lastMask;
}

static constexpr size_t NONE = SIZE_MAX;

/**
 * The highest granule from `limit` up to below `end` whose mark bit is
 * `value`, or NONE.
 */
static size_t highestBelow(const MarkWord* bits, size_t end, size_t limit,
                           bool value) {
    if (end <= limit) {
        return NONE;
    }
    MarkWord const flip = value ? 0 : ~(MarkWord)0;
    size_t const last = end - 1;
    size_t w = last / MARK_BITS;
    MarkWord word =
        (bits[w] ^ flip) & (~(MarkWord)0 >> (MARK_BITS - 1 - last % MARK_BITS));
    while (word == 0) {
        if (w * MARK_BITS <= limit) {
            return NONE;
        }
        w--;
        word = bits[w] ^ flip;
    }
    size_t const found = w * MARK_BITS + MARK_BITS - 1 - clz(word);
    return found >= limit ? found : NONE;
}

static gc_oop_t mark_object(gc_oop_t oop) {
    if (IS_TAGGED(oop)) {
        return oop;
    }
    auto* obj = (AbstractVMObject*)oop;
    assert(obj != nullptr);
    if (!isCollected(obj)) {
        return oop;
    }

    // a pointer into a marked object, like a frame's stack pointer, stops
    // here too
    size_t const granule = theHeap->granuleIndex(obj);
    if (isMarked(theHeap->markBits, granule)) {
        return oop;
    }
    assert(IsValidObject(obj));
    markGranules(theHeap->markBits, granule, objectSize(obj) / GRANULE);

    if (markStackSize < markStackLimit) {
        markStack[markStackSize++] = obj;
    } else {
        // marked, but its fields aren't: it's gray, and its pagelet is
        // scanned again
        size_t const pagelet = theHeap->pageletIndex(obj);
        theHeap->grayBits[pagelet / 8] |= (uint8_t)(1U << (pagelet % 8));
        markStackOverflowed = true;
    }
    return oop;
}

static void drainMarkStack() {
    while (markStackSize > 0) {
        markStack[--markStackSize]->WalkObjects(mark_object);
    }
}

/**
 * Walks the old objects that start in dirty cards: those that may point into
 * young space. In the pagelet that young space ends in, the objects below
 * oldBottom are young, and are left out. With `forget`, the barrier
 * forgets that it saw them, as the cards are cleared.
 */
static void walkDirtyCards(walk_heap_fn walk, bool drain,
                           [[maybe_unused]] bool forget) {
    size_t const end = theHeap->pageletIndex(theHeap->top);
    for (size_t pagelet = theHeap->pageletIndex(theHeap->oldBottom);
         pagelet < end; pagelet++) {
        if (theHeap->cards[pagelet] != DIRTY_CARD) {
            continue;
        }
        char* const pageletEnd = theHeap->pageletStart(pagelet + 1);
        char* p = theHeap->firstObjectIn(pagelet);
        if (p == nullptr) {
            continue;
        }
        if (p < theHeap->oldBottom) {
            p = theHeap->oldBottom;
        }
        while (p < pageletEnd && p < theHeap->top) {
            auto* obj = (AbstractVMObject*)p;
            obj->WalkObjects(walk);
#ifndef FRUCTUS
            // the barrier sees it again
            if (forget) {
                obj->SetGCField(OLD);
            }
#endif
            if (drain) {
                drainMarkStack();
            }
            p += objectSize(obj);
        }
    }
}

void SlidingCollector::markLiveObjects(bool major) {
    static_assert(sizeof(AbstractVMObject*) == sizeof(size_t));
    markStack = (AbstractVMObject**)theHeap->liveCounts;
    markStackSize = 0;
    // so that stress mode tests the overflows
    markStackLimit = gcStressMode ? 4 : MARK_STACK_ENTRIES;
    markStackOverflowed = false;

    Universe::WalkGlobals(mark_object);
#ifdef FRUCTUS
    FructusImage::WalkRoots(mark_object, /*onlyRemembered=*/!major);
#endif
    drainMarkStack();
    if (!major) {
        walkDirtyCards(mark_object, /*drain=*/true, /*forget=*/false);
    }
    while (markStackOverflowed) {
        markStackOverflowed = false;
        rescanGrayPagelets();
    }
}

void SlidingCollector::rescanGrayPagelets() {
    size_t const first = theHeap->pageletIndex(low);
    size_t const end = theHeap->pageletIndex(high - 1) + 1;
    for (size_t pagelet = first; pagelet < end; pagelet++) {
        uint8_t const bit = (uint8_t)(1U << (pagelet % 8));
        if ((theHeap->grayBits[pagelet / 8] & bit) == 0) {
            continue;
        }
        theHeap->grayBits[pagelet / 8] &= (uint8_t)~bit;

        // Walk the marked objects that start in the pagelet. Those that are
        // black already are walked again, which does no harm.
        char* const pageletEnd = theHeap->pageletStart(pagelet + 1);
        char* p = theHeap->firstObjectIn(pagelet);
        assert(p != nullptr);
        while (p < pageletEnd && p < high) {
            auto* obj = (AbstractVMObject*)p;
            if (isMarked(theHeap->markBits, theHeap->granuleIndex(p))) {
                obj->WalkObjects(mark_object);
                drainMarkStack();
            }
            p += objectSize(obj);
        }
    }
}

#ifndef FRUCTUS
/** After a collection, the host's heap grows until it has this much room. */
static constexpr size_t MIN_YOUNG_SPACE = (size_t)1024 * 1024;
#endif

#ifndef FRUCTUS
static_assert(OLD == MASK_OBJECT_IS_OLD && SEEN == MASK_SEEN_BY_WRITE_BARRIER);
#endif

static constexpr size_t GRANULES_PER_COUNT = MARK_BITS * MARK_WORDS_PER_COUNT;

void SlidingCollector::countLiveGranules() {
    size_t const firstCount = theHeap->granuleIndex(low) / GRANULES_PER_COUNT;
    // A count that covers high also covers old objects in a minor
    // collection, but they have no mark bits.
    size_t const endCount =
        (theHeap->granuleIndex(high) + GRANULES_PER_COUNT - 1) /
        GRANULES_PER_COUNT;

    theHeap->liveCounts[endCount] = 0;
    for (size_t c = endCount; c > firstCount; c--) {
        size_t live = theHeap->liveCounts[c];
        for (size_t w = 0; w < MARK_WORDS_PER_COUNT; w++) {
            live +=
                popcount(theHeap->markBits[(c - 1) * MARK_WORDS_PER_COUNT + w]);
        }
        theHeap->liveCounts[c - 1] = live;
    }
}

/** The number of live granules at and above `granule`. */
static size_t liveGranulesFrom(size_t granule) {
    size_t const w = granule / MARK_BITS;
    size_t const c = w / MARK_WORDS_PER_COUNT;
    size_t live = theHeap->liveCounts[c + 1];
    for (size_t above = w + 1; above < (c + 1) * MARK_WORDS_PER_COUNT;
         above++) {
        live += popcount(theHeap->markBits[above]);
    }
    return live + popcount(theHeap->markBits[w] >> (granule % MARK_BITS));
}

/** Where an address in a live object is after the slide. */
static char* newAddress(const char* p) {
    size_t const granule = theHeap->granuleIndex(p);
    assert(isMarked(theHeap->markBits, granule));
    return high - GRANULE * liveGranulesFrom(granule) +
           (p - theHeap->base) % GRANULE;
}

/**
 * Updates a pointer during the slide. The object it points to may have been
 * overwritten already, by objects that moved up into its place, so neither
 * this nor the walks that call it may look at it.
 */
static gc_oop_t update_pointer(gc_oop_t oop) {
    if (IS_TAGGED(oop)) {
        return oop;
    }
    assert(oop != nullptr);
    if (!isCollected(oop)) {
        return oop;
    }
    return (gc_oop_t)newAddress((const char*)oop);
}

void SlidingCollector::slide() {
    size_t const firstGranule = theHeap->granuleIndex(low);
    size_t const firstPagelet = theHeap->pageletIndex(low);

    // The runs record where their objects start. In the pagelet that high is
    // in, oldBottom is the first object above it.
    size_t const highPagelet = theHeap->pageletIndex(high);
    memset(&theHeap->objectStarts[firstPagelet], NO_OBJECT_START,
           highPagelet - firstPagelet);
    if (high < theHeap->top) {
        theHeap->setObjectStart(high);
    }

    // Each run of set mark bits is a block of live objects, the lowest of
    // which starts at the bottom of the run. The runs move up as blocks, from
    // the top down, so a run only overwrites what has moved already.
    char* destination = high;
    size_t end = theHeap->granuleIndex(high);
    for (;;) {
        size_t const highest =
            highestBelow(theHeap->markBits, end, firstGranule,
                         /*value=*/true);
        if (highest == NONE) {
            break;
        }
        size_t const lowest =
            highestBelow(theHeap->markBits, highest, firstGranule,
                         /*value=*/false);
        size_t const runStart = lowest == NONE ? firstGranule : lowest + 1;

        char* const from = theHeap->base + runStart * GRANULE;
        size_t const size = (highest + 1 - runStart) * GRANULE;
        char* const to = destination - size;
        if (to != from) {
            memmove(to, from, size);
        }

        // the first object of the run in each pagelet is the lowest one
        size_t lastPagelet = NONE;
        for (char* p = to; p < destination;) {
            auto* obj = (AbstractVMObject*)p;
            obj->WalkObjects(update_pointer);
#ifndef FRUCTUS
            // it's old now, and the barrier hasn't seen it
            obj->SetGCField(OLD);
#endif
            size_t const pagelet = theHeap->pageletIndex(p);
            if (pagelet != lastPagelet) {
                theHeap->setObjectStart(p);
                lastPagelet = pagelet;
            }
            p += objectSize(obj);
        }

        destination = to;
        end = runStart;
    }
    theHeap->allocation = destination;

    if (DEBUG) {
        memset(low, 0xFF, (size_t)(destination - low));
    }
}

void SlidingCollector::Collect() {
#ifndef FRUCTUS
    Timer::GCTimer.Resume();
#endif
    theHeap = heap;
    theHeap->resetGCTrigger();

    // In stress mode, every fourth collection is major, to test both.
    static size_t collections = 0;
    collections++;
    auto const committed = (size_t)(theHeap->top - theHeap->bottom);
    auto const old = (size_t)(theHeap->top - theHeap->oldBottom);
#ifdef FRUCTUS
    // The heap can't grow, so a major collection runs when old space has
    // taken more than half the room the last one left.
    size_t const room = committed - theHeap->liveAfterMajor;
    bool const oldIsFull = old - theHeap->liveAfterMajor > room / 2;
#else
    // the heap grows after a major collection that leaves it half full
    bool const oldIsFull = old > committed / 4 * 3;
#endif
    bool const major = theHeap->majorAtNextCollection ||
                       theHeap->growAtNextCollection || oldIsFull ||
                       (gcStressMode && collections % 4 == 0);
    theHeap->majorAtNextCollection = false;
    theHeap->lastCollectionWasMajor = major;
    DebugLog(major ? "Sliding major collection\n"
                   : "Sliding minor collection\n");

#if DEBUG
    verifyRememberedSet();
#endif
    low = theHeap->allocation;
    high = major ? theHeap->top : theHeap->oldBottom;

    markLiveObjects(major);
    countLiveGranules();
    // Old objects don't move, and the mark bits say where the objects they
    // point to go, so they can be updated before the slide.
    if (!major) {
        walkDirtyCards(update_pointer, /*drain=*/false, /*forget=*/true);
    }
    slide();
    // the mark bits say where the objects were, to update the roots with
    Universe::WalkGlobals(update_pointer);
#ifdef FRUCTUS
    FructusImage::WalkRoots(update_pointer, /*onlyRemembered=*/!major);
#endif

    // the mark bits are clear between collections
    size_t const firstWord = theHeap->granuleIndex(low) / MARK_BITS;
    size_t const endWord =
        (theHeap->granuleIndex(high) + MARK_BITS - 1) / MARK_BITS;
    memset(&theHeap->markBits[firstWord], 0,
           (endWord - firstWord) * sizeof(MarkWord));

    // everything is old now
    theHeap->oldBottom = theHeap->allocation;
#ifdef FRUCTUS
    // the image's cards too
    CleanFructusCards();
#else
    size_t const firstCard = theHeap->pageletIndex(low);
    memset(&theHeap->cards[firstCard], CLEAN_CARD,
           theHeap->pageletIndex(theHeap->top) - firstCard);
#endif

#if DEBUG
    verify();
#endif

    if (major) {
        auto const live = (size_t)(theHeap->top - theHeap->allocation);
        theHeap->liveAfterMajor = live;
#ifndef FRUCTUS
        // grow if the live objects take more than half
        if (theHeap->growAtNextCollection || live > committed / 2) {
            theHeap->grow();
        }
#endif
        theHeap->growAtNextCollection = false;
    }
#ifndef FRUCTUS
    // and if there's little room for young objects
    while ((size_t)(theHeap->allocation - theHeap->bottom) < MIN_YOUNG_SPACE &&
           theHeap->grow()) {
    }

    Timer::GCTimer.Halt();
#endif
}

#if DEBUG
// the object being verified, which may point into itself
static char* verifiedObject;
static char* verifiedObjectEnd;

static gc_oop_t check_pointer(gc_oop_t oop) {
    if (!IS_TAGGED(oop) && theHeap->Contains(oop)) {
        assert((char*)oop >= theHeap->allocation);
        if ((char*)oop < verifiedObject || (char*)oop >= verifiedObjectEnd) {
            assert(IsValidObject((vm_oop_t)oop));
        }
    }
    return oop;
}

/**
 * Checks that every old object that points into young space is in a dirty
 * card, so a missing write barrier fails where the pointer is, not when the
 * object it points to has been collected.
 */
static char* checkedHolder;

static gc_oop_t check_remembered(gc_oop_t oop) {
    if (!IS_TAGGED(oop) && oop >= (gc_oop_t)theHeap->allocation &&
        oop < (gc_oop_t)theHeap->oldBottom) {
        if (theHeap->cards[theHeap->pageletIndex(checkedHolder)] !=
            DIRTY_CARD) {
            ErrorPrintf("old object %p points to young %p, card is clean\n",
                        (void*)checkedHolder, (void*)oop);
            assert(false);
        }
    }
    return oop;
}

void SlidingCollector::verifyRememberedSet() {
    for (char* p = theHeap->oldBottom; p < theHeap->top;) {
        auto* obj = (AbstractVMObject*)p;
        checkedHolder = p;
        obj->WalkObjects(check_remembered);
        p += objectSize(obj);
    }
}

void SlidingCollector::verify() {
    size_t lastPagelet = theHeap->pageletIndex(theHeap->allocation) - 1;
    for (char* p = theHeap->allocation; p < theHeap->top;) {
        auto* obj = (AbstractVMObject*)p;
        assert(IsValidObject(obj));
  #ifndef FRUCTUS
        assert(obj->GetGCField() == OLD);
  #endif
        verifiedObject = p;
        verifiedObjectEnd = p + objectSize(obj);
        obj->WalkObjects(check_pointer);

        size_t const pagelet = theHeap->pageletIndex(p);
        if (pagelet != lastPagelet) {
            // the pagelets in between are covered by the previous object
            for (size_t covered = lastPagelet + 1; covered < pagelet;
                 covered++) {
                assert(theHeap->firstObjectIn(covered) == nullptr);
            }
            assert(theHeap->firstObjectIn(pagelet) == p);
            lastPagelet = pagelet;
        }
        p += objectSize(obj);
    }
}
#endif
