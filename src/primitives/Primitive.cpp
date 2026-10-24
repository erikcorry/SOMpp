#include "Primitive.h"

#include <assert.h>
#include <cstddef>

#include "../memory/Heap.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMClass.h"  // NOLINT(misc-include-cleaner) it's required to make the types complete
#include "../vmobjects/VMFrame.h"
#include "../vmobjects/VMSymbol.h"  // NOLINT(misc-include-cleaner) it's required to make the types complete

static vm_oop_t pHolder(vm_oop_t rcvr) {
    auto* self = static_cast<VMInvokable*>(rcvr);
    vm_oop_t holder = self->GetHolder();
    assert(holder != nullptr);
    return holder;
}

static vm_oop_t pSignature(vm_oop_t rcvr) {
    auto* self = static_cast<VMInvokable*>(rcvr);
    return self->GetSignature();
}

static void pInvokeOnWith(VMFrame* frame) {
    // REM: this is a clone with _Method mInvokeOnWith
    auto* args = static_cast<VMArray*>(frame->Pop());
    auto* rcvr = frame->Pop();
    auto* mthd = static_cast<VMInvokable*>(frame->Pop());

    frame->PushIntoCurrent(rcvr);

    size_t const num_args = args->GetNumberOfIndexableFields();
    for (size_t i = 0; i < num_args; i++) {
        vm_oop_t arg = args->GetIndexableField(i);
        frame->PushIntoCurrent(arg);
    }
    mthd->Invoke(frame);

    // put the arguments back if the invokable couldn't allocate what it
    // needed, so that the interpreter can run this send again
    if (GetHeap<HEAP_CLS>()->isRetryRequested()) {
        for (size_t i = 0; i <= num_args; i++) {
            frame->PopVoid();
        }
        frame->PushIntoCurrent(mthd);
        frame->PushIntoCurrent(rcvr);
        frame->PushIntoCurrent(args);
    }
}

_Primitive::_Primitive() {
    Add("signature", &pSignature, false);
    Add("holder", &pHolder, false);
    Add("invokeOn:with:", &pInvokeOnWith, false);
}
