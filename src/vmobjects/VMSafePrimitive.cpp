#include "VMSafePrimitive.h"

#include <assert.h>

#include "../compiler/CompileAttempt.h"
#include "../memory/Heap.h"
#include "../misc/debug.h"
#include "../misc/defs.h"
#include "../primitivesCore/Primitives.h"
#include "../vm/Print.h"
#ifndef FRUCTUS
  #include "../vm/Symbols.h"
  #include "../vm/Universe.h"
#endif
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

#ifdef FRUCTUS
// fructus's routines never send
static inline bool errorAsked() {
    return false;
}

static inline gc_oop_t* sendError(VMFrame* /*frame*/,
                                  gc_oop_t* /*receiver*/,
                                  gc_oop_t* sp) {
    return sp;
}
#else
// The message of the #error: a routine asked for, see SendError(). It's
// sent before anything can collect.
static vm_oop_t errorMessage = nullptr;

vm_oop_t VMSafePrimitive::SendError(const char* message) {
    errorMessage = Universe::NewString(message);
    return errorMessage;
}

static inline bool errorAsked() {
    return errorMessage != nullptr;
}

// Sends #error: with the message the routine asked for, in place of the
// primitive's send, whose receiver is at `receiver` on the stack and whose
// last argument is at sp: the receiver stays, and the message replaces the
// first argument, or goes above the receiver of a unary primitive, see
// VMFrame::EXTRA_STACK_SLOTS. Changes nothing if it fails.
static gc_oop_t* sendError(VMFrame* frame, gc_oop_t* receiver, gc_oop_t* sp) {
    vm_oop_t const message = errorMessage;
    errorMessage = nullptr;
    VMSymbol* selector = SymbolFor("error:");
    if (selector == nullptr) {
        return sp;
    }
    gc_oop_t const replaced = receiver[1];
    receiver[1] = store_with_separate_barrier(message);
    VMInvokable* invokable =
        CLASS_OF(load_ptr(*receiver))->LookupInvokable(selector);
    gc_oop_t* const top = invokable->Invoke(frame, receiver + 1);
    if (GetHeap<HEAP_CLS>()->isRetryRequested()) {
        receiver[1] = replaced;
        return sp;
    }
    return top;
}
#endif

gc_oop_t* VMSafeUnaryPrimitive::Invoke(VMFrame* frame, gc_oop_t* sp) {
    vm_oop_t result = prim.pointer(load_ptr(*sp));
    if (!succeeded(result)) {
        return sp;
    }
    if (errorAsked()) {
        return sendError(frame, sp, sp);
    }
    *sp = store_with_separate_barrier(result);
    return sp;
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

    vm_oop_t result = prim.pointer(leftObj, rightObj);
    if (!succeeded(result)) {
        return sp;
    }
    if (errorAsked()) {
        return sendError(frame, sp - 1, sp);
    }
    sp--;
    *sp = store_with_separate_barrier(result);
    return sp;
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

    vm_oop_t result = prim.pointer(self, arg1, arg2);
    if (!succeeded(result)) {
        return sp;
    }
    if (errorAsked()) {
        return sendError(frame, sp - 2, sp);
    }
    sp -= 2;
    *sp = store_with_separate_barrier(result);
    return sp;
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
