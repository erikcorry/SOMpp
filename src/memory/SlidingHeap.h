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
 *
 * On the host it reserves the address space for its largest size with mmap,
 * and makes the part below its top that it uses accessible with mprotect, so
 * it grows downwards, without moving anything.
 */

#include <cstddef>
#include <cstdint>

#include "../misc/defs.h"
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

    uint8_t* objectStarts;
    SlidingLayout::MarkWord* markBits;
    size_t* liveCounts;
    uint8_t* grayBits;

    size_t reservedSize;
    bool growAtNextCollection{false};
};
