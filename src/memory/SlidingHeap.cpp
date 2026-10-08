#include "SlidingHeap.h"

#include <cstddef>
#include <cstdint>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../misc/defs.h"  // NOLINT(misc-include-cleaner)
#include "../vm/Print.h"
#include "Heap.h"
#include "SlidingCollector.h"

using namespace SlidingLayout;

// the smallest reservation, so the heap can grow
static constexpr size_t MIN_RESERVED_SIZE = (size_t)512 * 1024 * 1024;

static size_t roundUp(size_t n, size_t unit) {
    return (n + unit - 1) / unit * unit;
}

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

    size_t const pagelets = reservedSize / PAGELET;
    size_t const markWords = reservedSize / GRANULE / MARK_BITS;
    objectStarts = static_cast<uint8_t*>(allocateTable(pagelets));
    memset(objectStarts, NO_OBJECT_START, pagelets);
    markBits =
        static_cast<MarkWord*>(allocateTable(markWords * sizeof(MarkWord)));
    liveCounts = static_cast<size_t*>(
        allocateTable((markWords / MARK_WORDS_PER_COUNT + 1) * sizeof(size_t)));
    grayBits = static_cast<uint8_t*>(allocateTable(roundUp(pagelets, 8) / 8));

    bottom = top - size;
    if (mprotect(bottom, size, PROT_READ | PROT_WRITE) != 0) {
        ErrorExit("unable to allocate heap memory");
    }
}

SlidingHeap::~SlidingHeap() {
    size_t const pagelets = reservedSize / PAGELET;
    size_t const markWords = reservedSize / GRANULE / MARK_BITS;
    munmap(base, reservedSize);
    munmap(objectStarts, pagelets);
    munmap(markBits, markWords * sizeof(MarkWord));
    munmap(liveCounts, (markWords / MARK_WORDS_PER_COUNT + 1) * sizeof(size_t));
    munmap(grayBits, roundUp(pagelets, 8) / 8);
}

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
    while (size > (size_t)(allocation - bottom)) {
        if (!grow()) {
            return false;
        }
    }
    return true;
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

char* SlidingHeap::firstObjectIn(size_t pagelet) const {
    uint8_t const start = objectStarts[pagelet];
    if (start == NO_OBJECT_START) {
        return nullptr;
    }
    auto const address = (uintptr_t)pageletStart(pagelet);
    return (char*)((address & ~(uintptr_t)0xff) | start);
}
