#include "SlidingHeap.h"

#include <cstddef>
#include <cstdint>
#include <string.h>
#ifndef FRUCTUS
  #include <sys/mman.h>
  #include <unistd.h>
#endif

#include "../misc/PlacementNew.h"
#include "../misc/defs.h"  // NOLINT(misc-include-cleaner)
#include "../vm/Print.h"
#include "Heap.h"
#include "SlidingCollector.h"

using namespace SlidingLayout;

static constexpr size_t roundUp(size_t n, size_t unit) {
    return (n + unit - 1) / unit * unit;
}

/**
 * The mark words of a heap of `size` bytes, in whole groups of a live count,
 * since the counting reads whole groups. Those past the top stay clear.
 */
static constexpr size_t markWordsFor(size_t size) {
    return roundUp(size / GRANULE / MARK_BITS, MARK_WORDS_PER_COUNT);
}

/** The live counts' table, which is the mark stack too. */
static constexpr size_t liveCountEntries(size_t markWords) {
    // and one for the end
    size_t const counts = markWords / MARK_WORDS_PER_COUNT + 1;
    return counts > MARK_STACK_ENTRIES ? counts : MARK_STACK_ENTRIES;
}

#ifdef FRUCTUS
// The region the linker leaves between the stack and the program, see
// ld/fructus-sim.ld in the fructus repository: the heap and its tables.
extern "C" char __heap_start[];
extern "C" char __heap_end[];

/** The bytes of the side tables of a heap of `size` bytes. */
static constexpr size_t tablesSize(size_t size) {
    size_t const pagelets = size / PAGELET;
    size_t const markWords = markWordsFor(size);
    return pagelets + markWords * sizeof(MarkWord) +
           liveCountEntries(markWords) * sizeof(size_t) +
           roundUp(pagelets, 8) / 8;
}

/** Takes `size` bytes of `*free` for a table. */
static void* takeTable(char** free, size_t size) {
    void* const table = *free;
    *free += roundUp(size, sizeof(size_t));
    return table;
}

// The heap can't grow. It takes what the linker left below the program, with
// its tables first. Its pagelets are aligned, so that firstObjectIn() can
// rebuild an address from its low byte, and so that the heap's cards are
// those of zp_hi that the barrier indexes by the address alone.
static char collectorStorage[sizeof(SlidingCollector)]
    __attribute__((aligned(alignof(SlidingCollector))));

SlidingHeap::SlidingHeap(size_t /*objectSpaceSize*/)
    : Heap<SlidingHeap>(new (collectorStorage) SlidingCollector(this)) {
    top = (char*)((uintptr_t)__heap_end & ~(uintptr_t)(PAGELET - 1));
    auto const region = (size_t)(top - __heap_start);
    size_t size = region & ~(size_t)(PAGELET - 1);
    // the tables round up to words, a few bytes each
    while (tablesSize(size) + 4 * sizeof(size_t) + size > region) {
        size -= PAGELET;
    }
    reservedSize = size;
    base = top - size;
    bottom = base;
    allocation = top;
    oldBottom = top;
    stackBase = base;
    stackEnd = base;

    size_t const pagelets = size / PAGELET;
    size_t const markWords = markWordsFor(size);
    char* free = __heap_start;
    objectStarts = static_cast<uint8_t*>(takeTable(&free, pagelets));
    memset(objectStarts, NO_OBJECT_START, pagelets);
    markBits = static_cast<MarkWord*>(
        takeTable(&free, markWords * sizeof(MarkWord)));
    memset(markBits, 0, markWords * sizeof(MarkWord));
    liveCounts = static_cast<size_t*>(
        takeTable(&free, liveCountEntries(markWords) * sizeof(size_t)));
    grayBits =
        static_cast<uint8_t*>(takeTable(&free, roundUp(pagelets, 8) / 8));
    memset(grayBits, 0, roundUp(pagelets, 8) / 8);
    assert(free <= base);
    // the image's cards too, which start out as zeros, that is dirty
    CleanFructusCards();
    cards = (uint8_t*)FRUCTUS_CARDS + (uintptr_t)base / PAGELET;
}

// it lives as long as the program
SlidingHeap::~SlidingHeap() = default;

bool SlidingHeap::grow() {
    return false;
}

// Each bump takes room from the heap for good, so the stack grows by just
// what the frame needs.
bool SlidingHeap::GrowStack(char* end) {
    if (end > allocation) {
        allocationFailed((size_t)(end - stackEnd));
        return false;
    }
    stackEnd = end;
    bottom = end;
    return true;
}
#else
// the smallest reservation, so the heap can grow
static constexpr size_t MIN_RESERVED_SIZE = (size_t)512 * 1024 * 1024;
// the frame stack's reservation, which is made accessible as it grows
static constexpr size_t STACK_RESERVED_SIZE = (size_t)256 * 1024 * 1024;

static void* reserve(size_t size) {
    void* const p = mmap(nullptr, size, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        ErrorExit("unable to reserve heap memory");
    }
    return p;
}

/** Zeroed memory for a side table, which uses RAM only where it's used. */
static void* allocateTable(size_t size) {
    void* const p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        ErrorExit("unable to allocate the heap's tables");
    }
    return p;
}

SlidingHeap::SlidingHeap(size_t objectSpaceSize)
    : Heap<SlidingHeap>(new SlidingCollector(this)) {
    auto const pageSize = (size_t)sysconf(_SC_PAGESIZE);
    size_t const size = roundUp(objectSpaceSize, pageSize);
    reservedSize = roundUp(max(MIN_RESERVED_SIZE, 4 * size),
                           pageSize * MARK_BITS * MARK_WORDS_PER_COUNT);

    base = static_cast<char*>(reserve(reservedSize));
    top = base + reservedSize;
    bottom = top;
    allocation = top;
    oldBottom = top;

    size_t const pagelets = reservedSize / PAGELET;
    size_t const markWords = reservedSize / GRANULE / MARK_BITS;
    objectStarts = static_cast<uint8_t*>(allocateTable(pagelets));
    memset(objectStarts, NO_OBJECT_START, pagelets);
    markBits =
        static_cast<MarkWord*>(allocateTable(markWords * sizeof(MarkWord)));
    liveCounts = static_cast<size_t*>(
        allocateTable(liveCountEntries(markWords) * sizeof(size_t)));
    grayBits = static_cast<uint8_t*>(allocateTable(roundUp(pagelets, 8) / 8));
    cards = static_cast<uint8_t*>(allocateTable(pagelets));
    memset(cards, CLEAN_CARD, pagelets);

    bottom = top - size;
    if (mprotect(bottom, size, PROT_READ | PROT_WRITE) != 0) {
        ErrorExit("unable to allocate heap memory");
    }

    stackReservedSize = STACK_RESERVED_SIZE;
    stackBase = static_cast<char*>(reserve(stackReservedSize));
    stackEnd = stackBase;
    stackAccessible = stackBase;
}

SlidingHeap::~SlidingHeap() {
    size_t const pagelets = reservedSize / PAGELET;
    size_t const markWords = reservedSize / GRANULE / MARK_BITS;
    munmap(base, reservedSize);
    munmap(objectStarts, pagelets);
    munmap(markBits, markWords * sizeof(MarkWord));
    munmap(liveCounts, liveCountEntries(markWords) * sizeof(size_t));
    munmap(grayBits, roundUp(pagelets, 8) / 8);
    munmap(cards, pagelets);
    munmap(stackBase, stackReservedSize);
}

bool SlidingHeap::grow() {
    auto const size = (size_t)(top - bottom);
    size_t const newSize = min(2 * size, reservedSize);
    if (newSize == size) {
        return false;
    }
    char* const newBottom = top - newSize;
    if (mprotect(newBottom, (size_t)(bottom - newBottom),
                 PROT_READ | PROT_WRITE) != 0) {
        return false;
    }
    bottom = newBottom;
    return true;
}

// The end is where the frames need it, as on fructus, and the pages up to it
// are made accessible as it passes them.
bool SlidingHeap::GrowStack(char* end) {
    if (end > stackAccessible) {
        auto const pageSize = (size_t)sysconf(_SC_PAGESIZE);
        char* const newAccessible =
            stackBase + roundUp((size_t)(end - stackBase), pageSize);
        if (newAccessible > stackBase + stackReservedSize ||
            mprotect(stackAccessible, (size_t)(newAccessible - stackAccessible),
                     PROT_READ | PROT_WRITE) != 0) {
            allocationFailed((size_t)(end - stackEnd));
            return false;
        }
        stackAccessible = newAccessible;
    }
    stackEnd = end;
    return true;
}
#endif

void* SlidingHeap::AllocateObject(size_t size) {
    size = AllocationSize(size);
    if (size > (size_t)(allocation - bottom)) {
        return allocationFailed(size);
    }
    allocation -= size;
    // the newest object is the lowest, so it's the first in its pagelet
    setObjectStart(allocation);
    if (gcStressMode) {
        requestGC();
    }
    return allocation;
}

bool SlidingHeap::MakeRoomFor(size_t size) {
    size = AllocationSize(size);
    if (size <= (size_t)(allocation - bottom)) {
        return true;
    }
    // a minor collection frees only the young objects
    if (!lastCollectionWasMajor) {
        majorAtNextCollection = true;
        gc->Collect();
    }
    while (size > (size_t)(allocation - bottom)) {
        if (!grow()) {
            return false;
        }
    }
    return true;
}

char* SlidingHeap::firstObjectIn(size_t pagelet) const {
    uint8_t const start = objectStarts[pagelet];
    if (start == NO_OBJECT_START) {
        return nullptr;
    }
    auto const address = (uintptr_t)pageletStart(pagelet);
    return (char*)((address & ~(uintptr_t)0xff) | start);
}
