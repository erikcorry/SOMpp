#include "VMTrivialMethod.h"

#include <assert.h>
#include <cstddef>
#ifndef FRUCTUS
#include <vector>
#endif

#ifndef FRUCTUS
#include "../compiler/BytecodeGenerator.h"
  #include "../compiler/CompileAttempt.h"
#endif
#ifndef FRUCTUS
#include "../compiler/MethodGenerationContext.h"
#endif
#ifndef FRUCTUS
#include "../compiler/Variable.h"
#endif
#include "../interpreter/Interpreter.h"
#include "../memory/Heap.h"
#include "../misc/debug.h"
#include "../misc/defs.h"
#include "../vm/Print.h"
#include "../vm/Statistics.h"
#include "../vm/Universe.h"
#include "AbstractObject.h"
#include "ObjectFormats.h"
#include "VMFrame.h"

#ifndef FRUCTUS
VMTrivialMethod* MakeLiteralReturn(VMSymbol* sig, vector<Variable>& arguments,
                                   vm_oop_t literal) {
    auto* result =
        new (GetHeap<HEAP_CLS>(), 0) VMLiteralReturn(sig, arguments, literal);
    if (result == nullptr) {
        AbandonIfCompiling();
        return nullptr;
    }
    recordStat(Allocation, "VMLiteralReturn", result->GetObjectSize());
    return result;
}

VMTrivialMethod* MakeGlobalReturn(VMSymbol* sig, vector<Variable>& arguments,
                                  VMSymbol* globalName) {
    auto* result =
        new (GetHeap<HEAP_CLS>(), 0) VMGlobalReturn(sig, arguments, globalName);
    if (result == nullptr) {
        AbandonIfCompiling();
        return nullptr;
    }
    recordStat(Allocation, "VMGlobalReturn", result->GetObjectSize());
    return result;
}

VMTrivialMethod* MakeGetter(VMSymbol* sig, vector<Variable>& arguments,
                            size_t fieldIndex) {
    auto* result =
        new (GetHeap<HEAP_CLS>(), 0) VMGetter(sig, arguments, fieldIndex);
    if (result == nullptr) {
        AbandonIfCompiling();
        return nullptr;
    }
    recordStat(Allocation, "VMGetter", result->GetObjectSize());
    return result;
}

VMTrivialMethod* MakeSetter(VMSymbol* sig, vector<Variable>& arguments,
                            size_t fieldIndex, size_t argIndex) {
    auto* result = new (GetHeap<HEAP_CLS>(), 0)
        VMSetter(sig, arguments, fieldIndex, argIndex);
    if (result == nullptr) {
        AbandonIfCompiling();
        return nullptr;
    }
    recordStat(Allocation, "VMSetter", result->GetObjectSize());
    return result;
}
#endif

// The trivial methods replace the receiver and the arguments on the stack
// with their result.
gc_oop_t* VMLiteralReturn::Invoke(VMFrame* /*frame*/, gc_oop_t* sp) {
    sp -= numberOfArguments - 1;
    *sp = store_with_separate_barrier(load_ptr(literal));
    return sp;
}

gc_oop_t* VMLiteralReturn::Invoke1(VMFrame* /*frame*/, gc_oop_t* sp) {
    assert(numberOfArguments == 1);
    *sp = store_with_separate_barrier(load_ptr(literal));
    return sp;
}

AbstractVMObject* VMLiteralReturn::CloneForMovingGC() const {
    auto* prim =
        new (GetHeap<HEAP_CLS>(), 0 ALLOC_MATURE) VMLiteralReturn(*this);
    return prim;
}

#if DEBUG
std::string VMLiteralReturn::AsDebugString() const {
    return "VMLiteralReturn(" + AS_OBJ(load_ptr(literal))->AsDebugString() +
           ")";
}
#endif

void VMLiteralReturn::WalkObjects(walk_heap_fn walk) {
    VMInvokable::WalkObjects(walk);
    literal = walk(literal);
}

#ifndef FRUCTUS
void VMLiteralReturn::InlineInto(MethodGenerationContext& mgenc,
                                 const Parser& parser, bool /*mergeScope*/) {
    EmitPUSHCONSTANT(mgenc, parser, load_ptr(literal));
}
#endif

gc_oop_t* VMGlobalReturn::Invoke(VMFrame* frame, gc_oop_t* sp) {
    vm_oop_t value = Universe::GetGlobal(load_ptr(globalName));
    if (value == nullptr) {
        return sendUnknownGlobal(frame, sp);
    }

    sp -= numberOfArguments - 1;
    *sp = store_with_separate_barrier(value);
    return sp;
}

gc_oop_t* VMGlobalReturn::Invoke1(VMFrame* frame, gc_oop_t* sp) {
    assert(numberOfArguments == 1);

    vm_oop_t value = Universe::GetGlobal(load_ptr(globalName));
    if (value == nullptr) {
        return sendUnknownGlobal(frame, sp);
    }

    *sp = store_with_separate_barrier(value);
    return sp;
}

// Sends #unknownGlobal: in place of the send of the method, which its result
// is the result of, from the frame's stack, see VMInvokable::InvokeFromVM().
// Changes nothing if it fails.
gc_oop_t* VMGlobalReturn::sendUnknownGlobal(VMFrame* frame, gc_oop_t* sp) {
#ifdef FRUCTUS
    // all globals are in the image, so this is an error
    (void)frame;
    GetInterpreter()->SendUnknownGlobal(load_ptr(globalName));
    return sp;
#else
    frame->SetStackPointer(sp);
    std::vector<vm_oop_t> arguments(numberOfArguments);
    for (int i = numberOfArguments - 1; i >= 0; i -= 1) {
        arguments[i] = frame->Pop();
    }
    if (!GetInterpreter()->SendUnknownGlobal(load_ptr(globalName))) {
        for (vm_oop_t arg : arguments) {
            frame->PushIntoCurrent(arg);
        }
    }
    return GetInterpreter()->GetFrame()->GetStackPointer();
#endif
}

#ifndef FRUCTUS
void VMGlobalReturn::InlineInto(MethodGenerationContext& mgenc,
                                const Parser& parser, bool /*mergeScope*/) {
    EmitPUSHGLOBAL(mgenc, parser, load_ptr(globalName));
}
#endif

void VMGlobalReturn::WalkObjects(walk_heap_fn walk) {
    VMInvokable::WalkObjects(walk);
    globalName = (GCSymbol*)walk(globalName);
}

#if DEBUG
std::string VMGlobalReturn::AsDebugString() const {
    return "VMGlobalReturn(" + AS_OBJ(load_ptr(globalName))->AsDebugString() +
           ")";
}
#endif

AbstractVMObject* VMGlobalReturn::CloneForMovingGC() const {
    auto* prim =
        new (GetHeap<HEAP_CLS>(), 0 ALLOC_MATURE) VMGlobalReturn(*this);
    return prim;
}

gc_oop_t* VMGetter::Invoke(VMFrame* /*frame*/, gc_oop_t* sp) {
    sp -= numberOfArguments - 1;
    vm_oop_t self = load_ptr(*sp);

    assert(self != nullptr);

    vm_oop_t result = nullptr;
    if (unlikely(IS_TAGGED(self))) {
        result = nullptr;
        ErrorExit("Integers do not have fields!");
    } else {
        result = ((VMObject*)self)->GetField(fieldIndex);
    }

    *sp = store_with_separate_barrier(result);
    return sp;
}

gc_oop_t* VMGetter::Invoke1(VMFrame* /*frame*/, gc_oop_t* sp) {
    assert(numberOfArguments == 1);
    vm_oop_t self = load_ptr(*sp);

    assert(self != nullptr);

    vm_oop_t result = nullptr;
    if (unlikely(IS_TAGGED(self))) {
        result = nullptr;
        ErrorExit("Integers do not have fields!");
    } else {
        result = ((VMObject*)self)->GetField(fieldIndex);
    }

    *sp = store_with_separate_barrier(result);
    return sp;
}

#ifndef FRUCTUS
void VMGetter::InlineInto(MethodGenerationContext& mgenc,
                          const Parser& /*parser*/, bool /*mergeScope*/) {
    EmitPushFieldWithIndex(mgenc, fieldIndex);
}
#endif

AbstractVMObject* VMGetter::CloneForMovingGC() const {
    auto* prim = new (GetHeap<HEAP_CLS>(), 0 ALLOC_MATURE) VMGetter(*this);
    return prim;
}

void VMGetter::WalkObjects(walk_heap_fn walk) {
    VMInvokable::WalkObjects(walk);
}

#if DEBUG
std::string VMGetter::AsDebugString() const {
    return "VMGetter(fieldIndex: " + to_string(fieldIndex) + ")";
}
#endif

gc_oop_t* VMSetter::Invoke(VMFrame* /*frame*/, gc_oop_t* sp) {
    // the receiver stays, as the result
    sp -= numberOfArguments - 1;
    vm_oop_t value = load_ptr(sp[argIndex]);
    vm_oop_t self = load_ptr(*sp);
    assert(self != nullptr);
    assert(value != nullptr);

    if (unlikely(IS_TAGGED(self))) {
        ErrorExit("Integers do not have fields!");
    } else {
        ((VMObject*)self)->SetField(fieldIndex, value);
    }

    return sp;
}

gc_oop_t* VMSetter::Invoke1(VMFrame* /*unused*/, gc_oop_t* /*sp*/) {
    ErrorExit("VMSetter::Invoke1 should not be reachable");
}

#ifndef FRUCTUS
void VMSetter::InlineInto(MethodGenerationContext& /*mgenc*/,
                          const Parser& /*parser*/, bool /*mergeScope*/) {
    ErrorExit("We don't currently support blocks for trivial setters");
}
#endif

AbstractVMObject* VMSetter::CloneForMovingGC() const {
    auto* prim = new (GetHeap<HEAP_CLS>(), 0 ALLOC_MATURE) VMSetter(*this);
    return prim;
}

void VMSetter::WalkObjects(walk_heap_fn walk) {
    VMInvokable::WalkObjects(walk);
}

#if DEBUG
std::string VMSetter::AsDebugString() const {
    return "VMSetter(fieldIndex: " + to_string(fieldIndex) +
           ", argIndex: " + to_string(argIndex) + ")";
}
#endif

#ifndef FRUCTUS
void VMTrivialMethod::Dump(const char* /*indent*/, bool /*printObjects*/) {
    DebugPrint("<trivial method>\n");
}
#endif
