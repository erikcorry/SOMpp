#pragma once

#include <string.h>

#include "Heap.h"

class CopyingHeap : public Heap<CopyingHeap> {
    friend class CopyingCollector;

public:
    explicit CopyingHeap(size_t objectSpaceSize);
    void* AllocateObject(size_t size);
    bool MakeRoomFor(size_t size);

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
