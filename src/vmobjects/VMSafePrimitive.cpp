#include "VMSafePrimitive.h"

#include <assert.h>

#include "../memory/Heap.h"
#include "../misc/debug.h"
#include "../misc/defs.h"
#include "../primitivesCore/Primitives.h"
#include "../vm/Print.h"
#include "AbstractObject.h"
#include "ObjectFormats.h"
#include "VMClass.h"
#include "VMFrame.h"
#include "VMMethod.h"
#include "VMSymbol.h"

VMInvokable* VMSafePrimitive::GetSafeUnary(VMSymbol* sig, UnaryPrim prim) {
    auto* p = new (GetHeap<HEAP_CLS>(), 0) VMSafeUnaryPrimitive(sig, prim);
    return p;
}

// A routine answers nullptr when an allocation failed, and the interpreter
// will run the send again after a collection. So the arguments stay on the
// stack until the routine has answered.
static inline bool succeeded(vm_oop_t result) {
    assert(result != nullptr || GetHeap<HEAP_CLS>()->isRetryRequested());
    return result != nullptr;
}

VMFrame* VMSafeUnaryPrimitive::Invoke(VMFrame* frame) {
    vm_oop_t result = prim.pointer(frame->Top());
    if (succeeded(result)) {
        frame->SetTop(result);
    }
    return nullptr;
}

VMFrame* VMSafeUnaryPrimitive::Invoke1(VMFrame* frame) {
    vm_oop_t result = prim.pointer(frame->Top());
    if (succeeded(result)) {
        frame->SetTop(result);
    }
    return nullptr;
}

VMInvokable* VMSafePrimitive::GetSafeBinary(VMSymbol* sig, BinaryPrim prim) {
    auto* p = new (GetHeap<HEAP_CLS>(), 0) VMSafeBinaryPrimitive(sig, prim);
    return p;
}

VMFrame* VMSafeBinaryPrimitive::Invoke(VMFrame* frame) {
    vm_oop_t rightObj = frame->GetStackElement(0);
    vm_oop_t leftObj = frame->GetStackElement(1);

    vm_oop_t result = prim.pointer(leftObj, rightObj);
    if (succeeded(result)) {
        frame->PopVoid();
        frame->SetTop(result);
    }
    return nullptr;
}

VMFrame* VMSafeBinaryPrimitive::Invoke1(VMFrame* /*frame*/) {
    ErrorExit("Unary invoke on binary primitive");
}

VMInvokable* VMSafePrimitive::GetSafeTernary(VMSymbol* sig, TernaryPrim prim) {
    auto* p = new (GetHeap<HEAP_CLS>(), 0) VMSafeTernaryPrimitive(sig, prim);
    return p;
}

VMFrame* VMSafeTernaryPrimitive::Invoke(VMFrame* frame) {
    vm_oop_t arg2 = frame->GetStackElement(0);
    vm_oop_t arg1 = frame->GetStackElement(1);
    vm_oop_t self = frame->GetStackElement(2);

    vm_oop_t result = prim.pointer(self, arg1, arg2);
    if (succeeded(result)) {
        frame->PopVoid();
        frame->PopVoid();
        frame->SetTop(result);
    }
    return nullptr;
}

VMFrame* VMSafeTernaryPrimitive::Invoke1(VMFrame* /*frame*/) {
    ErrorExit("Unary invoke on binary primitive");
}

#if DEBUG
std::string VMSafePrimitive::AsDebugString() const {
    return "SafePrim(" + GetClass()->GetName()->GetStdString() + ">>#" +
           GetSignature()->GetStdString() + ")";
}
#endif

AbstractVMObject* VMSafeUnaryPrimitive::CloneForMovingGC() const {
    auto* prim =
        new (GetHeap<HEAP_CLS>(), 0 ALLOC_MATURE) VMSafeUnaryPrimitive(*this);
    return prim;
}

AbstractVMObject* VMSafeBinaryPrimitive::CloneForMovingGC() const {
    auto* prim =
        new (GetHeap<HEAP_CLS>(), 0 ALLOC_MATURE) VMSafeBinaryPrimitive(*this);
    return prim;
}

AbstractVMObject* VMSafeTernaryPrimitive::CloneForMovingGC() const {
    auto* prim =
        new (GetHeap<HEAP_CLS>(), 0 ALLOC_MATURE) VMSafeTernaryPrimitive(*this);
    return prim;
}

#ifndef FRUCTUS
void VMSafePrimitive::InlineInto(MethodGenerationContext& /*mgenc*/,
                                 const Parser& /*parser*/,
                                 bool /*mergeScope*/) {
    ErrorExit(
        "VMPrimitive::InlineInto is not supported, and should not be reached");
}
#endif

#ifndef FRUCTUS
void VMSafePrimitive::Dump(const char* /*indent*/, bool /*printObjects*/) {
    DebugPrint("<primitive>\n");
}
#endif
