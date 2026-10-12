/*
 *
 *
 Copyright (c) 2007 Michael Haupt, Tobias Pape, Arne Bergmann
 Software Architecture Group, Hasso Plattner Institute, Potsdam, Germany
 http://www.hpi.uni-potsdam.de/swa/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#include "VMFrame.h"

#include <cstddef>
#include <cstdint>
#include <string.h>

#ifndef FRUCTUS
#include "../compiler/Disassembler.h"
#endif
#include "../memory/Heap.h"
#include "../misc/defs.h"
#include "../vm/Globals.h"
#include "../vm/Print.h"
#include "ObjectFormats.h"
#include "VMClass.h"
#include "VMMethod.h"
#include "VMObject.h"
#include "VMObjectBase.h"  // NOLINT(misc-include-cleaner) needed for some GCs
#include "VMSymbol.h"

VMFrame* VMFrame::CloneForMovingGC() const {
    size_t const addSpace = totalObjectSize - sizeof(VMFrame);
    auto* clone =
        new (GetHeap<HEAP_CLS>(), addSpace ALLOC_MATURE) VMFrame(*this);
    void* destination = SHIFTED_PTR(clone, sizeof(VMFrame));
    const void* source = SHIFTED_PTR(this, sizeof(VMFrame));
    memcpy(destination, source, addSpace);
    // locals and stack_ptr point into the frame
    clone->locals =
        (gc_oop_t*)SHIFTED_PTR(clone, (size_t)locals - (size_t)this);
    clone->stack_ptr =
        (gc_oop_t*)SHIFTED_PTR(clone, (size_t)stack_ptr - (size_t)this);
    return clone;
}

const size_t VMFrame::VMFrameNumberOfFields = 0;

VMFrame* VMFrame::GetOuterContext() {
    VMFrame* current = this;
    while (current->HasContext()) {
        current = current->GetContext();
    }
    return current;
}

void VMFrame::WalkObjects(walk_heap_fn walk) {
    // VMFrame is not a proper SOM object any longer, we don't have a class for
    // it. clazz = (VMClass*) walk(clazz);

#if GC_TYPE == SLIDING
    // The sliding collector moves pointers into the frame like any other,
    // since a live object's whole extent is marked. The other collectors
    // would take them for objects; the copying ones relocate them in
    // CloneForMovingGC().
    assert((void*)locals < SHIFTED_PTR(this, totalObjectSize));
    assert((void*)stack_ptr < SHIFTED_PTR(this, totalObjectSize));
    locals = (gc_oop_t*)walk((gc_oop_t)locals);
    stack_ptr = (gc_oop_t*)walk((gc_oop_t)stack_ptr);
#endif

    if (previousFrame != nullptr) {
        previousFrame = static_cast<GCFrame*>(walk(previousFrame));
    }
    if (context != nullptr) {
        context = static_cast<GCFrame*>(walk(context));
    }
    method = static_cast<GCMethod*>(walk(method));

    // all other fields are indexable via arguments array
    // --> until end of Frame
    gc_oop_t* const arguments = this->arguments();
    size_t i = 0;
    while (arguments + i <= stack_ptr) {
        if (arguments[i] != nullptr) {
            arguments[i] = walk(arguments[i]);
        }
        i++;
    }
}

#ifndef FRUCTUS
void VMFrame::PrintBytecode() const {
    Disassembler::DumpMethod(GetMethod(), "  ");
}
#endif

#if DEBUG
static void print_oop(gc_oop_t vmo) {
    if (vmo == nullptr) {
        Print("nullptr\n");
    } else if (vmo == nilObject) {
        Print("NIL_OBJECT\n");
    } else {
        AbstractVMObject const* const o = AS_OBJ(vmo);
        Print(o->AsDebugString() + "\n");
    }
}

void VMFrame::PrintStack() const {
    Print(GetMethod()->AsDebugString() +
          ", bc: " + to_string(GetBytecodeIndex()) + "\n" +
          "Args: " + to_string(GetMethod()->GetNumberOfArguments()) +
          " Locals: " + to_string(GetMethod()->GetNumberOfLocals()) +
          " MaxStack:" +
          to_string(GetMethod()->GetMaximumNumberOfStackElements()) + "\n");

    for (uint8_t i = 0; i < GetMethod()->GetNumberOfArguments(); i++) {
        Print("   arg " + to_string(i) + ": ");
        print_oop(arguments()[i]);
    }

    size_t local_offset = 0;
    for (size_t i = 0; i < GetMethod()->GetNumberOfLocals(); i++) {
        Print("   loc " + to_string(i) + ": ");
        print_oop(locals[i]);
        local_offset++;
    }

    size_t const max = GetMethod()->GetMaximumNumberOfStackElements();
    for (size_t i = 0; i < max; i++) {
        if (stack_ptr == &locals[local_offset + i]) {
            Print("-> stk " + to_string(i) + ": ");
        } else {
            Print("   stk " + to_string(i) + ": ");
        }
        print_oop(locals[local_offset + i]);
    }

    auto* end = (gc_oop_t*)SHIFTED_PTR(this, totalObjectSize);
    size_t i = 0;
    while (&locals[local_offset + max + i] < end) {
        if (stack_ptr == &locals[local_offset + max + i]) {
            Print("->estk " + to_string(i) + ": ");
        } else {
            Print("  estk " + to_string(i) + ": ");
        }
        print_oop(locals[local_offset + max + i]);
        i++;
    }
}
#endif

void VMFrame::SetLocal(uint8_t index, uint8_t contextLevel, vm_oop_t value) {
    VMFrame* context = GetContextLevel(contextLevel);
    context->SetLocal(index, value);
}

void VMFrame::SetArgument(uint8_t index, uint8_t contextLevel, vm_oop_t value) {
    VMFrame* context = GetContextLevel(contextLevel);
    context->SetArgument(index, value);
}

#ifdef FRUCTUS
// the methods have no names on fructus, so this is the pc of each frame,
// just after its send, which tools/fructus-trace makes into methods and
// source lines with the debug info of the image
void VMFrame::PrintStackTrace() const {
    Print("pcs:");
    for (VMFrame const* frame = this; frame != nullptr;
         frame = load_ptr(frame->previousFrame)) {
        auto pc = (uintptr_t)(frame->GetMethod()->GetBytecodes() +
                              frame->GetBytecodeIndex());
        char hex[] = " 0000";
        for (size_t i = 4; i > 0; i--, pc >>= 4U) {
            hex[i] = "0123456789abcdef"[pc & 0xfU];
        }
        Print(hex);
    }
    Print("\n");
}
#else
void VMFrame::PrintStackTrace() const {
    for (VMFrame const* frame = this; frame != nullptr;
         frame = load_ptr(frame->previousFrame)) {
        VMMethod const* const meth = frame->GetMethod();

        if (meth->GetHolder() == load_ptr(nilObject)) {
            Print("nil");
        } else {
            VMSymbol const* name = meth->GetHolder()->GetName();
            PrintChars(name->GetRawChars(), name->GetStringLength());
        }
        VMSymbol const* sig = meth->GetSignature();
        Print(">>#");
        PrintChars(sig->GetRawChars(), sig->GetStringLength());
        Print("\n");
    }
}
#endif

void VMFrame::CopyArgumentsFrom(VMFrame* frame) {
    // this is the current frame, see VMMethod::Invoke()
    // copy arguments from frame:
    // - arguments are at the top of the stack of frame.
    // - copy them into the argument area of the current frame
    size_t const num_args = GetMethod()->GetNumberOfArguments();
    for (size_t i = 0; i < num_args; ++i) {
        SetArgumentOfCurrent(i, frame->GetStackElement(num_args - 1 - i));
    }
}

#if DEBUG
std::string VMFrame::AsDebugString() const {
    return "VMFrame(" + GetMethod()->AsDebugString() + ")";
}
#endif

void VMFrame::ResetBytecodeIndex() {
    bytecodeIndex = 0;
}
