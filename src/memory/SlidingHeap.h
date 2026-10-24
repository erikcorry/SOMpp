#pragma once
/**
 * The sliding heap of ideas/gc.md, the one fructus will use. It is one
 * contiguous block, allocated downwards from its top with a bump pointer, and
 * a collection compacts it by sliding the live objects up, in order. So there
 * are no free lists and no fragmentation, and a collection allocates nothing.
 *
 * Its side tables, which cover the whole extent of the heap:
 *  - The object starts: a byte per pagelet, the low byte of the address of
 *    the lowest object that starts in it, or NO_OBJECT_START. So the objects
 *    of a pagelet can be walked from a known object start.
 *  - The mark bits: a bit per granule. A live object has the bits of its
 *    whole extent set, so the number of live granules at and above an
 *    address says where the address moves to, and the lowest granule of a
 *    run of set bits is the start of an object.
 *  - The live counts: the number of mark bits set at and above each group of
 *    MARK_WORDS_PER_COUNT mark words, so that finding where an address moves
 *    to takes at most that many popcounts.
 *  - The gray bits: a bit per pagelet, for the marked objects that didn't fit
 *    on the mark stack, whose pagelets are scanned again.
 *  - The cards, the remembered set: a byte per pagelet, which the write
 *    barrier makes DIRTY_CARD when it stores a pointer into an object that
 *    starts in the pagelet.
 *
 * It's generational, with instant promotion. The objects that survived the
 * last collection are old, from oldBottom to the top, and those allocated
 * since are young, below it. A minor collection collects only the young
 * objects, with the old objects in dirty cards as roots, and slides the
 * survivors up to oldBottom, so afterwards there are no young objects and no
 * dirty cards. A major collection collects the whole heap the same way.
 *
 * On the host it reserves the address space for its largest size with mmap,
 * and makes the part below its top that it uses accessible with mprotect, so
 * it grows downwards, without moving anything.
 *
 * The frames of the methods that make no blocks are on the frame stack, see
 * ideas/frame-stack.md. It grows up, from stackBase to stackEnd, which is
 * bumped to the end of a frame that needs more room, and never shrinks, so
 * it's where the deepest stack so far ended. On fructus it's at
 * the bottom of the heap's memory, and its end is the heap's bottom, so the
 * bump allocator stops at it. The host has a reservation of its own for it.
 * The collector never moves or frees anything in it, since it's below low,
 * see SlidingCollector.cpp; the interpreter walks the live part as a root.
 */

#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <string.h>

#include "../misc/defs.h"
#include "../vmobjects/ObjectFormats.h"
#include "Heap.h"

namespace SlidingLayout {
#ifdef FRUCTUS
// a mark word covers 64 bytes, and a live count 256
constexpr size_t GRANULE = 4;
constexpr size_t MARK_WORDS_PER_COUNT = 4;
#else
constexpr size_t GRANULE = sizeof(void*);
constexpr size_t MARK_WORDS_PER_COUNT = 1;
#endif
/** The object starts store the low byte of an address, so it's 256 at most.
 */
constexpr size_t PAGELET = 128;
/** Objects start at even addresses. */
constexpr uint8_t NO_OBJECT_START = 1;
typedef uintptr_t MarkWord;
constexpr size_t MARK_BITS = sizeof(MarkWord) * 8;
constexpr size_t MARK_STACK_ENTRIES = 128;
/**
 * Dirty is 0 because on fructus the barrier is `cards[holder >> 7] = cards`:
 * the cards are 256 aligned, so the low byte of their address is 0, and the
 * inlined barrier needs no register for the value it stores.
 */
constexpr uint8_t DIRTY_CARD = 0;
constexpr uint8_t CLEAN_CARD = 1;
#ifndef FRUCTUS
// VMObjectBase.h's MASK_OBJECT_IS_OLD and MASK_SEEN_BY_WRITE_BARRIER
constexpr size_t OLD = 1U << 1U;
constexpr size_t SEEN = 1U << 2U;
#endif
#ifdef FRUCTUS
/**
 * The cards cover the low 32K, where everything that is written is, see
 * cmake/fructus-som.ld. They are at the zero page base.
 */
constexpr uintptr_t FRUCTUS_CARDS = 0x200;
constexpr uintptr_t FRUCTUS_WRITABLE_END = 0x8000;

/** Cleans all the cards, of the heap and the image. */
inline void CleanFructusCards() {
  // gcc takes a constant address for an object of size 0
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wstringop-overflow"
    memset((uint8_t*)FRUCTUS_CARDS, CLEAN_CARD,
           FRUCTUS_WRITABLE_END / PAGELET);
  #pragma GCC diagnostic pop
}
#endif

static_assert(PAGELET <= 256 && PAGELET % GRANULE == 0, "pagelet size");
static_assert(GRANULE % sizeof(void*) == 0, "granule size");

/** The size an object of `size` bytes takes in the heap. */
constexpr size_t AllocationSize(size_t size) {
    return (size + GRANULE - 1) & ~(GRANULE - 1);
}
}  // namespace SlidingLayout

class SlidingHeap : public Heap<SlidingHeap> {
    friend class SlidingCollector;

public:
    explicit SlidingHeap(size_t objectSpaceSize);
    ~SlidingHeap();
    void* AllocateObject(size_t size);
    bool MakeRoomFor(size_t size);

    // it grows without a collection, but only at one, or when a retry needs
    // more room, so that it behaves like a heap of a fixed size otherwise
    static constexpr bool COMPILATION_RETRIES_GROW_HEAP = true;
    void GrowAtNextCollection() { growAtNextCollection = true; }

    [[nodiscard]] bool Contains(const void* p) const {
        return p >= (const void*)base && p < (const void*)top;
    }

    /**
     * Bumps the frame stack's end to `end` or past it. If there's no room,
     * it asks for a retry, as AllocateObject() does, and answers false. The
     * interpreter keeps the end, and the top, see Interpreter.h.
     */
    bool GrowStack(char* end);

    [[nodiscard]] char* GetStackBase() const { return stackBase; }
    [[nodiscard]] char* GetStackEnd() const { return stackEnd; }

    /** Whether `p` is in the frame stack. */
    [[nodiscard]] bool InFrameStack(const void* p) const {
#ifdef FRUCTUS
        // everything below the stack's end is the stack or isn't a frame
        return p < (const void*)stackEnd;
#else
        return p >= (const void*)stackBase && p < (const void*)stackEnd;
#endif
    }

    /**
     * Remembers that `holder` may point into young space now.
     *
     * On the host, like the generational heap's barrier, it uses the GC
     * field in the holder's header: only an old holder that isn't remembered
     * yet gets its card dirtied, and is then SEEN until the next collection.
     * That branch nearly always goes the same way, and the header is in the
     * cache, since a field of the holder was just written.
     *
     * Fructus has no GC field, so its barrier filters only the small
     * integers, and doesn't need to know where young space is.
     */
    void writeBarrier(const void* holder, const void* value) {
#ifdef FRUCTUS
        if (IS_TAGGED(value)) {
            return;
        }
#else
        (void)value;
#endif
        rememberObject(holder);
    }

    /**
     * Remembers `holder` whatever it points to, as the write barrier does
     * for a value that may be young. The interpreter stores into the current
     * frame without the barrier, and remembers the frame instead, see
     * remember_object().
     */
    void rememberObject(const void* holder) {
#ifdef FRUCTUS
        // The cards cover the low 32K, where every holder is. Their address
        // is the zero page base, and its low byte is 0, so the base register
        // is all the barrier needs.
        assert((uintptr_t)holder < SlidingLayout::FRUCTUS_WRITABLE_END);
        auto* const zpCards = (uint8_t*)SlidingLayout::FRUCTUS_CARDS;
  // gcc takes a constant address for an object of size 0
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wstringop-overflow"
  #pragma GCC diagnostic ignored "-Warray-bounds"
        zpCards[(uintptr_t)holder / SlidingLayout::PAGELET] =
            (uint8_t)(uintptr_t)zpCards;
  #pragma GCC diagnostic pop
#else
        // the GC field follows the vtable, as GenerationalHeap's barrier has
        // it, since VMObjectBase.h can't be included here
        auto* const gcField = (size_t*)holder + 1;
        if ((*gcField & (SlidingLayout::OLD | SlidingLayout::SEEN)) !=
            SlidingLayout::OLD) {
            return;
        }
        assert(Contains(holder));
        *gcField |= SlidingLayout::SEEN;
        cards[pageletIndex(holder)] = SlidingLayout::DIRTY_CARD;
#endif
    }

    // The layout, for the collector, whose functions walk the heap.

    [[nodiscard]] size_t granuleIndex(const void* p) const {
        return (size_t)((const char*)p - base) / SlidingLayout::GRANULE;
    }
    [[nodiscard]] size_t pageletIndex(const void* p) const {
        return (size_t)((const char*)p - base) / SlidingLayout::PAGELET;
    }
    [[nodiscard]] char* pageletStart(size_t pagelet) const {
        return base + pagelet * SlidingLayout::PAGELET;
    }
    void setObjectStart(const void* p) {
        objectStarts[pageletIndex(p)] = (uint8_t)(uintptr_t)p;
    }
    /** The lowest object that starts in the pagelet, or nullptr. */
    [[nodiscard]] char* firstObjectIn(size_t pagelet) const;

    /** Doubles the accessible part of the heap, if it can. */
    bool grow();

    // the reserved address space, with the heap at its top
    char* base;
    char* top;
    // the start of the accessible part
    char* bottom;
    // the lowest object; the free space is below it
    char* allocation;
    // the lowest old object, or top if there are none
    char* oldBottom;

    // the frame stack, see above
    char* stackBase;
    char* stackEnd;
#ifndef FRUCTUS
    size_t stackReservedSize;
    // the pages below it are accessible
    char* stackAccessible;
#endif

    uint8_t* objectStarts;
    SlidingLayout::MarkWord* markBits;
    size_t* liveCounts;
    uint8_t* grayBits;
    uint8_t* cards;

    size_t reservedSize;
    bool growAtNextCollection{false};
    bool majorAtNextCollection{false};
    bool lastCollectionWasMajor{false};
    // the size of the old objects after the last major collection
    size_t liveAfterMajor{0};
};
