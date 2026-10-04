#include "MarkSweepCollector.h"

#include <assert.h>
#include <cstddef>

#include "../misc/SimpleVector.h"
#include "../misc/debug.h"
#include "../vm/IsValidObject.h"
#include "../vm/Universe.h"
#include "../vmobjects/AbstractObject.h"
#include "../vmobjects/IntegerBox.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMFrame.h"
#include "MarkSweepHeap.h"

static size_t markedBytes = 0;
static SimpleVector<AbstractVMObject*> markStack;

static gc_oop_t mark_object(gc_oop_t oop) {
    if (IS_TAGGED(oop)) {
        return oop;
    }

    AbstractVMObject* obj = AS_OBJ(oop);
    assert(IsValidObject(obj));

    MarkSweepHeap const* heap = GetHeap<MarkSweepHeap>();
    if (obj->GetGCField() == heap->GetMark()) {
        return oop;
    }

    obj->SetGCField(heap->GetMark());
    if (heap->Contains(obj)) {
        markedBytes +=
            MarkSweepCollector::GetAllocationSize(obj->GetObjectSize());
    }
    markStack.push_back(obj);
    return oop;
}

void MarkSweepCollector::Collect() {
    DebugLog("MarkSweep Collect\n");

#ifndef FRUCTUS
    Timer::GCTimer.Resume();
#endif
    heap->resetGCTrigger();

    markReachableObjects();
    heap->sweep();

#ifndef FRUCTUS
    Timer::GCTimer.Halt();
#endif
}

void MarkSweepCollector::markReachableObjects() {
    markedBytes = 0;
    markStack.clear();

    Universe::WalkGlobals(mark_object);

    while (!markStack.empty()) {
        AbstractVMObject* obj = markStack.back();
        markStack.pop_back();
        obj->WalkObjects(mark_object);
    }

    heap->liveBytes = markedBytes;
}
