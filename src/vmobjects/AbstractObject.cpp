/*
 * AbstractVMObject.cpp
 *
 *  Created on: 10.03.2011
 *      Author: christian
 */

#include "AbstractObject.h"

#include <cstddef>
#include <cstdint>

#include "../interpreter/Interpreter.h"
#include "../vmobjects/ObjectFormats.h"
#include "VMClass.h"
#include "VMFrame.h"
#include "VMInvokable.h"
#include "VMSymbol.h"

void AbstractVMObject::Send(VMSymbol* selector, vm_oop_t* arguments,
                            size_t argc) {
    VMFrame* frame = GetInterpreter()->GetFrame();
    frame->Push(this);

    for (size_t i = 0; i < argc; ++i) {
        frame->Push(arguments[i]);
    }

    VMClass* cl = GetClass();
    VMInvokable* invokable = cl->LookupInvokable(selector);
    invokable->Invoke(frame);
}

som_int_t AbstractVMObject::GetFieldIndex(VMSymbol* fieldName) const {
    return GetClass()->LookupFieldIndex(fieldName);
}
