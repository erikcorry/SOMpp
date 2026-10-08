#include "SlidingCollector.h"

#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <string.h>

#include "../misc/Timer.h"
#include "../misc/debug.h"
#include "../misc/defs.h"  // NOLINT(misc-include-cleaner)
#include "../vm/IsValidObject.h"
#include "../vm/Universe.h"
#include "../vmobjects/AbstractObject.h"
#include "../vmobjects/ObjectFormats.h"
#include "Heap.h"
#include "SlidingHeap.h"

using namespace SlidingLayout;

// The collection in progress. On fructus the mark stack will have a fixed
// place in memory, which it shares with the live counts.
static SlidingHeap* theHeap;
static AbstractVMObject* markStack[MARK_STACK_ENTRIES];
static size_t markStackSize;
static size_t markStackLimit;
static bool markStackOverflowed;

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
    if (!theHeap->Contains(obj)) {
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

void SlidingCollector::markLiveObjects() {
    markStackSize = 0;
    // so that stress mode tests the overflows
    markStackLimit = gcStressMode ? 4 : MARK_STACK_ENTRIES;
    markStackOverflowed = false;

    Universe::WalkGlobals(mark_object);
    drainMarkStack();
    while (markStackOverflowed) {
        markStackOverflowed = false;
        rescanGrayPagelets();
    }
}

void SlidingCollector::rescanGrayPagelets() {
    size_t const first = theHeap->pageletIndex(theHeap->allocation);
    size_t const end = theHeap->pageletIndex(theHeap->top);
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
        while (p < pageletEnd && p < theHeap->top) {
            auto* obj = (AbstractVMObject*)p;
            if (isMarked(theHeap->markBits, theHeap->granuleIndex(p))) {
                obj->WalkObjects(mark_object);
                drainMarkStack();
            }
            p += objectSize(obj);
        }
    }
}

void SlidingCollector::countLiveGranules() {
    size_t const firstWord =
        theHeap->granuleIndex(theHeap->allocation) / MARK_BITS;
    size_t const endWord = theHeap->granuleIndex(theHeap->top) / MARK_BITS;
    size_t const firstCount = firstWord / MARK_WORDS_PER_COUNT;
    size_t const endCount = endWord / MARK_WORDS_PER_COUNT;

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
    return theHeap->top - GRANULE * liveGranulesFrom(granule) +
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
    if (!theHeap->Contains(oop)) {
        return oop;
    }
    return (gc_oop_t)newAddress((const char*)oop);
}

void SlidingCollector::slide() {
    char* const oldAllocation = theHeap->allocation;
    size_t const firstGranule = theHeap->granuleIndex(oldAllocation);
    size_t const firstPagelet = theHeap->pageletIndex(oldAllocation);

    // the runs record where their objects start
    memset(&theHeap->objectStarts[firstPagelet], NO_OBJECT_START,
           theHeap->pageletIndex(theHeap->top) - firstPagelet);

    // Each run of set mark bits is a block of live objects, the lowest of
    // which starts at the bottom of the run. The runs move up as blocks, from
    // the top down, so a run only overwrites what has moved already.
    char* destination = theHeap->top;
    size_t end = theHeap->granuleIndex(theHeap->top);
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
        memset(oldAllocation, 0xFF, (size_t)(destination - oldAllocation));
    }
}

void SlidingCollector::Collect() {
    DebugLog("Sliding Collect\n");

    Timer::GCTimer.Resume();
    theHeap = heap;
    theHeap->resetGCTrigger();
    char* const oldAllocation = theHeap->allocation;

    markLiveObjects();
    countLiveGranules();
    slide();
    // the mark bits say where the objects were, to update the roots with
    Universe::WalkGlobals(update_pointer);

    // the mark bits are clear between collections
    size_t const firstWord = theHeap->granuleIndex(oldAllocation) / MARK_BITS;
    size_t const endWord = theHeap->granuleIndex(theHeap->top) / MARK_BITS;
    memset(&theHeap->markBits[firstWord], 0,
           (endWord - firstWord) * sizeof(MarkWord));

#if DEBUG
    verify();
#endif

    // like a semispace, grow if the live objects take more than half
    auto const live = (size_t)(theHeap->top - theHeap->allocation);
    if (theHeap->growAtNextCollection ||
        live > (size_t)(theHeap->top - theHeap->bottom) / 2) {
        theHeap->grow();
    }
    theHeap->growAtNextCollection = false;

    Timer::GCTimer.Halt();
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

void SlidingCollector::verify() {
    size_t lastPagelet = theHeap->pageletIndex(theHeap->allocation) - 1;
    for (char* p = theHeap->allocation; p < theHeap->top;) {
        auto* obj = (AbstractVMObject*)p;
        assert(IsValidObject(obj));
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
