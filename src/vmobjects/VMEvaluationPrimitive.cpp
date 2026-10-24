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

#include "VMEvaluationPrimitive.h"

#include <assert.h>
#include <cstddef>
#include <cstdint>

#include "../interpreter/Interpreter.h"
#include "../memory/Heap.h"
#include "../misc/debug.h"
#include "../misc/defs.h"
#include "../vm/Print.h"
#ifndef FRUCTUS
  #include "../vm/Symbols.h"
#endif
#include "../vm/Universe.h"  // NOLINT(misc-include-cleaner) it's required to make the types complete
#include "ObjectFormats.h"
#include "VMBlock.h"
#include "VMFrame.h"
#include "VMMethod.h"
#include "VMSymbol.h"

#ifndef FRUCTUS
VMEvaluationPrimitive::VMEvaluationPrimitive(uint8_t argc)
    : VMInvokable(computeSignatureString(argc)), numberOfArguments(argc) {}
#endif

VMEvaluationPrimitive* VMEvaluationPrimitive::CloneForMovingGC() const {
    auto* evPrim =
        new (GetHeap<HEAP_CLS>(), 0 ALLOC_MATURE) VMEvaluationPrimitive(*this);
    return evPrim;
}

void VMEvaluationPrimitive::WalkObjects(walk_heap_fn walk) {
    VMInvokable::WalkObjects(walk);
}

#ifndef FRUCTUS
VMSymbol* VMEvaluationPrimitive::computeSignatureString(size_t argc) {
#define VALUE_S "value"
#define VALUE_LEN 5
#define WITH_S "with:"
#define WITH_LEN (4 + 1)
#define COLON_S ":"
    assert(argc > 0);

    // value, value:, value:with:, value:with:with:, ...
    char signatureString[VALUE_LEN + 1 + (WITH_LEN * 8) + 1];
    strcpy(signatureString, VALUE_S);
    if (argc > 1) {
        strcat(signatureString, COLON_S);
        --argc;
        while (--argc != 0) {
            // Add extra value: selector elements if necessary
            strcat(signatureString, WITH_S);
        }
    }

    // Return the signature string
    return SymbolFor(signatureString);
}
#endif

gc_oop_t* VMEvaluationPrimitive::Invoke(VMFrame* frm, gc_oop_t* sp) {
    // Get the block (the receiver) from the stack
    auto* block = static_cast<VMBlock*>(load_ptr(sp[1 - numberOfArguments]));

    // Get the context of the block...
    VMFrame* context = block->GetContext();
    VMInvokable* method = block->GetMethod();
    sp = method->Invoke(frm, sp);

    // Push set its context to be the one specified in the block
    VMFrame* const newFrame = GetInterpreter()->GetFrame();
    if (newFrame != frm) {
        newFrame->SetContextOfCurrent(context);
    }
    return sp;
}

gc_oop_t* VMEvaluationPrimitive::Invoke1(VMFrame* frm, gc_oop_t* sp) {
    assert(numberOfArguments == 1);
    // Get the block (the receiver) from the stack
    auto* block = static_cast<VMBlock*>(load_ptr(*sp));

    // Get the context of the block...
    VMFrame* context = block->GetContext();
    VMInvokable* method = block->GetMethod();
    sp = method->Invoke1(frm, sp);

    // Push set its context to be the one specified in the block
    VMFrame* const newFrame = GetInterpreter()->GetFrame();
    if (newFrame != frm) {
        newFrame->SetContextOfCurrent(context);
    }
    return sp;
}

#if DEBUG
std::string VMEvaluationPrimitive::AsDebugString() const {
    return "VMEvaluationPrimitive(" + to_string(numberOfArguments) + ")";
}
#endif

#define INVALID_INT_MARKER 0xFF

void VMEvaluationPrimitive::MarkObjectAsInvalid() {
    VMInvokable::MarkObjectAsInvalid();
    numberOfArguments = INVALID_INT_MARKER;
}

bool VMEvaluationPrimitive::IsMarkedInvalid() const {
    return numberOfArguments == INVALID_INT_MARKER;
}

#ifndef FRUCTUS
void VMEvaluationPrimitive::InlineInto(MethodGenerationContext& /*mgenc*/,
                                       const Parser& /*parser*/,
                                       bool /*mergeScope*/) {
    ErrorExit(
        "VMEvaluationPrimitive::InlineInto is not supported, and should not be "
        "reached");
}
#endif

#ifndef FRUCTUS
void VMEvaluationPrimitive::Dump(const char* /*indent*/,
                                 bool /*printObjects*/) {
    DebugPrint("<primitive>\n");
}
#endif
