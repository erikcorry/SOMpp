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

#include "VMPrimitive.h"

#include "../compiler/CompileAttempt.h"
#include "../interpreter/Interpreter.h"
#include "../memory/Heap.h"
#include "../misc/debug.h"
#include "../misc/defs.h"
#include "../primitivesCore/Primitives.h"
#include "../vm/Globals.h"  // NOLINT (misc-include-cleaner)
#include "../vm/Print.h"
#include "ObjectFormats.h"
#include "VMClass.h"
#include "VMFrame.h"
#include "VMMethod.h"
#include "VMSymbol.h"

VMInvokable* VMPrimitive::GetFramePrim(VMSymbol* sig, FramePrim prim) {
    return AbandonIfCompilingAndNull(new (GetHeap<HEAP_CLS>(), 0)
                                         VMPrimitive(sig, prim));
}

// The routine works on the frame's stack, and may push a frame, see
// InvokeFromVM().
gc_oop_t* VMPrimitive::Invoke(VMFrame* frm, gc_oop_t* sp) {
    frm->SetStackPointer(sp);
    prim.pointer(frm);
    return GetInterpreter()->GetFrame()->GetStackPointer();
}

VMPrimitive* VMPrimitive::CloneForMovingGC() const {
    auto* prim = new (GetHeap<HEAP_CLS>(), 0 ALLOC_MATURE) VMPrimitive(*this);
    return prim;
}

// not static: the fructus image refers to it by name, see vm/FructusImage.h
extern "C" void som_empty_primitive(VMFrame* frame) {
    ErrorPrint("undefined primitive called\n");
    frame->PrintStackTrace();
    ErrorExit("undefined primitive called");
}

VMInvokable* VMPrimitive::GetEmptyPrimitive(VMSymbol* sig, bool classSide) {
    return GetFramePrim(sig, FramePrim(&som_empty_primitive, classSide, 0));
}

bool VMPrimitive::IsEmpty() const {
    return prim.pointer == &som_empty_primitive;
}

#ifndef FRUCTUS
void VMPrimitive::InlineInto(MethodGenerationContext& /*mgenc*/,
                             const Parser& /*parser*/, bool /*mergeScope*/) {
    ErrorExit(
        "VMPrimitive::InlineInto is not supported, and should not be reached");
}
#endif

#if DEBUG
std::string VMPrimitive::AsDebugString() const {
    return "Primitive(" + GetClass()->GetName()->GetStdString() + ">>#" +
           GetSignature()->GetStdString() + ")";
}
#endif

#ifndef FRUCTUS
void VMPrimitive::Dump(const char* /*indent*/, bool /*printObjects*/) {
    DebugPrint("<primitive>\n");
}
#endif
