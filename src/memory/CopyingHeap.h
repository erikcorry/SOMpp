#pragma once

#include <string.h>

#include "Heap.h"

class CopyingHeap : public Heap<CopyingHeap> {
    friend class CopyingCollector;

public:
    explicit CopyingHeap(size_t objectSpaceSize);
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

    void* currentBuffer;
    void* currentBufferEnd;

    void* oldBuffer;
    void* oldBufferEnd;

    void* nextFreePosition;
    bool oldBufferIsValid{false};
    // the next collection doubles the semispaces
    bool increaseMemory{false};
};
