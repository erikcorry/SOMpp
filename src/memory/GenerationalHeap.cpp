#include "GenerationalHeap.h"

#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../vm/Print.h"
#include "../vmobjects/AbstractObject.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMObjectBase.h"
#include "GenerationalCollector.h"
#include "Heap.h"

using namespace std;

GenerationalHeap::GenerationalHeap(size_t objectSpaceSize)
    : Heap<GenerationalHeap>(new GenerationalCollector(this)),
      nursery(malloc(objectSpaceSize)),
      nursery_end((size_t)nursery + objectSpaceSize),
      nurserySize(objectSpaceSize), maxNurseryObjSize(objectSpaceSize / 2),
      nextFreePosition(nursery) {
    memset(nursery, 0x0, objectSpaceSize);
}

void* GenerationalHeap::AllocateNurseryObject(size_t size) {
    if (size > nursery_end - (size_t)nextFreePosition) {
        // An object bigger than the nursery wouldn't fit after a collection
        // either.
        if (!AllocationMustSucceed() && size <= nurserySize) {
            return allocationFailed(size);
        }
        // Divert to old space. The next minor collection, which the full
        // nursery asks for, marks the object old and scans old space.
        rememberedSetIsBankrupt = true;
        requestGC();
        return AllocateMatureObject(size);
    }
    void* newObject = nextFreePosition;
    nextFreePosition =
        static_cast<void*>(static_cast<char*>(nextFreePosition) + size);
    if (gcStressMode) {
        requestGC();
    }
    return newObject;
}

void* GenerationalHeap::AllocateMatureObject(size_t size) {
    void* newObject = malloc(size);
    if (newObject == nullptr) {
        ErrorPrint("\nFailed to allocate " + to_string(size) + " Bytes.\n");
        Quit(-1);
    }
    allocatedObjects.push_back(static_cast<AbstractVMObject*>(newObject));
    matureObjectsSize += size;
    if (gcStressMode) {
        requestGC();
    }
    return newObject;
}

void GenerationalHeap::rememberObject(VMObjectBase* holder) {
    assert(IsValidObject(holder));

    size_t const gcField = holder->GetGCField();
    if ((gcField & (MASK_OBJECT_IS_OLD | MASK_SEEN_BY_WRITE_BARRIER)) ==
        MASK_OBJECT_IS_OLD) {
        oldObjsWithRefToYoungObjs.push_back((size_t)holder);
        holder->SetGCField(gcField | MASK_SEEN_BY_WRITE_BARRIER);
    }
}

void GenerationalHeap::writeBarrier_OldHolder(VMObjectBase* holder,
                                              vm_oop_t referencedObject) {
    if (isObjectInNursery(referencedObject)) {
        oldObjsWithRefToYoungObjs.push_back((size_t)holder);
        holder->SetGCField(holder->GetGCField() | MASK_SEEN_BY_WRITE_BARRIER);
    }
}
