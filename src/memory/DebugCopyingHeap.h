#pragma once

#include <cstddef>

#include "DebugCopyingCollector.h"
#include "Heap.h"

class DebugCopyingHeap : public Heap<DebugCopyingHeap> {
    friend class DebugCopyingCollector;

public:
    explicit DebugCopyingHeap(size_t objectSpaceSize)
        : Heap<DebugCopyingHeap>(new DebugCopyingCollector(this)),
          currentHeapSize(objectSpaceSize) {}
    void* AllocateObject(size_t size);
    bool MakeRoomFor(size_t size);

    // it has no space to put what doesn't fit in it, but grows at a
    // collection
    static constexpr bool COMPILATION_RETRIES_GROW_HEAP = true;
    void GrowAtNextCollection() { increaseMemory = true; }

    bool IsInCurrentBuffer(AbstractVMObject* obj);
    bool IsInOldBufferAndOldBufferIsValid(AbstractVMObject* obj);

private:
    void switchBuffers(bool increaseMemory);
    void invalidateOldBuffer();

    vector<AbstractVMObject*> currentHeap;
    size_t currentHeapSize;
    size_t currentHeapUsage{0};

    vector<AbstractVMObject*> oldHeap;
    size_t oldHeapSize{0};

    bool oldHeapIsValid{false};
    // the next collection doubles the heap
    bool increaseMemory{false};
};
