#include "VMSafePrimitive.h"

#include <assert.h>

#include "../compiler/CompileAttempt.h"
#include "../interpreter/Interpreter.h"
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
    return AbandonIfCompilingAndNull(new (GetHeap<HEAP_CLS>(), 0)
                                         VMSafeUnaryPrimitive(sig, prim));
}

// A routine answers nullptr when an allocation failed, and the interpreter
// will run the send again after a collection. So the arguments stay on the
// stack until the routine has answered.
static inline bool succeeded(vm_oop_t result) {
    assert(result != nullptr || GetHeap<HEAP_CLS>()->isRetryRequested());
    return result != nullptr;
}

// On the host a routine may send #error:, which pushes onto the frame's
// stack, and pushes a frame of its own, see VMVector::sendError(): the frame
// has the top of its stack in its stack_ptr while the routine runs, and the
// result replaces the routine's arguments on the stack as it is afterwards.
// Fructus's routines never send.
#ifdef FRUCTUS
static inline void beforeRoutine(VMFrame* /*frame*/, gc_oop_t* /*sp*/) {}

static inline gc_oop_t* afterRoutine(VMFrame* /*frame*/, gc_oop_t* sp) {
    return sp;
}

static inline gc_oop_t* answer(VMFrame* /*frame*/, gc_oop_t* sp) {
    return sp;
}
#else
static inline void beforeRoutine(VMFrame* frame, gc_oop_t* sp) {
    frame->SetStackPointer(sp);
}

static inline gc_oop_t* afterRoutine(VMFrame* frame, gc_oop_t* /*sp*/) {
    return frame->GetStackPointer();
}

// the frame's top, or the top of the frame of #error:
static inline gc_oop_t* answer(VMFrame* frame, gc_oop_t* sp) {
    frame->SetStackPointer(sp);
    return GetInterpreter()->GetFrame()->GetStackPointer();
}
#endif

gc_oop_t* VMSafeUnaryPrimitive::Invoke(VMFrame* frame, gc_oop_t* sp) {
    beforeRoutine(frame, sp);
    vm_oop_t result = prim.pointer(load_ptr(*sp));
    if (!succeeded(result)) {
        return sp;
    }
    sp = afterRoutine(frame, sp);
    *sp = store_with_separate_barrier(result);
    return answer(frame, sp);
}

gc_oop_t* VMSafeUnaryPrimitive::Invoke1(VMFrame* frame, gc_oop_t* sp) {
    return VMSafeUnaryPrimitive::Invoke(frame, sp);
}

VMInvokable* VMSafePrimitive::GetSafeBinary(VMSymbol* sig, BinaryPrim prim) {
    return AbandonIfCompilingAndNull(new (GetHeap<HEAP_CLS>(), 0)
                                         VMSafeBinaryPrimitive(sig, prim));
}

gc_oop_t* VMSafeBinaryPrimitive::Invoke(VMFrame* frame, gc_oop_t* sp) {
    vm_oop_t rightObj = load_ptr(sp[0]);
    vm_oop_t leftObj = load_ptr(sp[-1]);

    beforeRoutine(frame, sp);
    vm_oop_t result = prim.pointer(leftObj, rightObj);
    if (!succeeded(result)) {
        return sp;
    }
    sp = afterRoutine(frame, sp) - 1;
    *sp = store_with_separate_barrier(result);
    return answer(frame, sp);
}

gc_oop_t* VMSafeBinaryPrimitive::Invoke1(VMFrame* /*frame*/, gc_oop_t* /*sp*/) {
    ErrorExit("Unary invoke on binary primitive");
}

VMInvokable* VMSafePrimitive::GetSafeTernary(VMSymbol* sig, TernaryPrim prim) {
    return AbandonIfCompilingAndNull(new (GetHeap<HEAP_CLS>(), 0)
                                         VMSafeTernaryPrimitive(sig, prim));
}

gc_oop_t* VMSafeTernaryPrimitive::Invoke(VMFrame* frame, gc_oop_t* sp) {
    vm_oop_t arg2 = load_ptr(sp[0]);
    vm_oop_t arg1 = load_ptr(sp[-1]);
    vm_oop_t self = load_ptr(sp[-2]);

    beforeRoutine(frame, sp);
    vm_oop_t result = prim.pointer(self, arg1, arg2);
    if (!succeeded(result)) {
        return sp;
    }
    sp = afterRoutine(frame, sp) - 2;
    *sp = store_with_separate_barrier(result);
    return answer(frame, sp);
}

gc_oop_t* VMSafeTernaryPrimitive::Invoke1(VMFrame* /*frame*/,
                                          gc_oop_t* /*sp*/) {
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
