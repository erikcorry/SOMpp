/*
 * AbstractVMObject.cpp
 *
 *  Created on: 10.03.2011
 *      Author: christian
 */

#include "AbstractObject.h"

#include <assert.h>
#include <cstddef>
#include <cstdint>

#include "../interpreter/Interpreter.h"
#include "../memory/Heap.h"
#include "../vmobjects/ObjectFormats.h"
#include "VMClass.h"
#include "VMFrame.h"
#include "VMInvokable.h"
#include "VMSymbol.h"

#ifndef FRUCTUS
// fructus has no sends from the VM: they are errors there
bool AbstractVMObject::Send(VMSymbol* selector, vm_oop_t* arguments,
                            size_t argc) {
    assert(!GetHeap<HEAP_CLS>()->isRetryRequested());
    VMFrame* frame = GetInterpreter()->GetFrame();
    frame->Push(this);

    for (size_t i = 0; i < argc; ++i) {
        frame->Push(arguments[i]);
    }

    VMClass* cl = GetClass();
    VMInvokable* invokable = cl->LookupInvokable(selector);
    invokable->Invoke(frame);

    // the invokable changed nothing if it failed, so the arguments are on top
    if (GetHeap<HEAP_CLS>()->isRetryRequested()) {
        for (size_t i = 0; i <= argc; ++i) {
            frame->PopVoid();
        }
        return false;
    }
    return true;
}
#endif

#ifndef FRUCTUS
// fructus has no reflection on field names
som_int_t AbstractVMObject::GetFieldIndex(VMSymbol* fieldName) const {
    return GetClass()->LookupFieldIndex(fieldName);
}
#endif
