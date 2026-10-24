#pragma once

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

#include <cstdint>
#include <type_traits>

#include "../vmobjects/VMArray.h"
#include "../vmobjects/VMMethod.h"

class Universe;

class VMFrame : public AbstractVMObject {
    friend class Universe;
    friend class Interpreter;
    friend class Shell;
    friend class VMMethod;

public:
    typedef GCFrame Stored;

#ifdef FRUCTUS
    // fructus makes none of the sends below
    static constexpr size_t EXTRA_STACK_SLOTS = 0;
#else
    /**
     * Stack slots beyond the method's maximum stack depth, for the sends the
     * VM makes that the compiler doesn't count, so that a frame never has to
     * grow: the Vector primitives send #error: with their receiver and
     * arguments still on the stack, #doesNotUnderstand:arguments: needs 3
     * slots, where the send that failed had at least 1, and #unknownGlobal:
     * and #escapedBlock: need 2, where the global or the block had 1.
     */
    static constexpr size_t EXTRA_STACK_SLOTS = 2;
#endif


    explicit VMFrame(size_t additionalBytes, VMMethod* method,
                     VMFrame* previousFrame)
        :
#ifdef FRUCTUS
          bytecodePointer(method->GetBytecodes()),
#else
          totalObjectSize(additionalBytes + sizeof(VMFrame)),
#endif
          previousFrame(store_root(previousFrame)), method(store_root(method)),
          locals(arguments() + method->GetNumberOfArguments()),
          stack_ptr(locals + method->GetNumberOfLocals() - 1) {
#ifdef FRUCTUS
        // the size comes from the method, see GetObjectSize()
        (void)additionalBytes;
#endif
        // The arguments are copied in still, and the stack needs nothing,
        // since nothing above stack_ptr is a root, see WalkObjects().
        for (gc_oop_t* local = locals; local <= stack_ptr; local++) {
            *local = nilObject;
        }
    }

    [[nodiscard]] som_int_t GetHash() const final {
        return 0; /* should never be called */
    }

    [[nodiscard]] inline VMClass* GetClass() const final { return nullptr; }

    /** The size of a frame of `method` in the heap. */
    static size_t SizeFor(const VMMethod* method) {
        return sizeof(VMFrame) + (method->GetNumberOfArguments() +
                                  method->GetNumberOfLocals() +
                                  method->GetMaximumNumberOfStackElements() +
                                  EXTRA_STACK_SLOTS) *
                                     sizeof(gc_oop_t);
    }

    /**
     * On fructus the size comes from the method, which is in the image and
     * never moves, so a frame needs no field for it. The host's methods are
     * in the heap, and a collector may move or free one while it needs the
     * size of a frame of it, so the frame has the size.
     */
    [[nodiscard]] inline size_t GetObjectSize() const final {
#ifdef FRUCTUS
        return SizeFor(GetMethod());
#else
        return totalObjectSize;
#endif
    }

    void MarkObjectAsInvalid() final {
        previousFrame = (GCFrame*)INVALID_GC_POINTER;
    }

    [[nodiscard]] bool IsMarkedInvalid() const final {
        return previousFrame == (GCFrame*)INVALID_GC_POINTER;
    }

    [[nodiscard]] inline VMFrame* GetPreviousFrame() const;
    inline void ClearPreviousFrame();
    [[nodiscard]] inline bool HasPreviousFrame() const;
    [[nodiscard]] inline bool IsBootstrapFrame() const;
    [[nodiscard]] inline VMFrame* GetContext() const;
    inline void SetContext(VMFrame* /*frm*/);
    [[nodiscard]] inline bool HasContext() const;
    [[nodiscard]] inline VMFrame* GetContextLevel(size_t lvl);
    VMFrame* GetOuterContext();
    [[nodiscard]] inline VMMethod* GetMethod() const;

    inline vm_oop_t Pop() {
        vm_oop_t result = load_ptr(*stack_ptr);
        stack_ptr--;
        return result;
    }

    inline void PopVoid() { stack_ptr--; }

    inline vm_oop_t Top() {
        vm_oop_t result = load_ptr(*stack_ptr);
        return result;
    }

    inline vm_oop_t Top2() {
        vm_oop_t result = load_ptr(*(stack_ptr - 1));
        return result;
    }

    inline void SetTop(vm_oop_t val) { store_ptr(*stack_ptr, val); }

    inline void Push(vm_oop_t obj) {
        assert(RemainingStackSize() > 0);
        ++stack_ptr;
        store_ptr(*stack_ptr, obj);
    }

    /* Stores that skip the write barrier. Only valid for tagged integers,
     * which are not heap objects, and for nil, true, and false. Being roots
     * keeps those alive, but isn't enough by itself: a minor collection moves
     * young objects, and only updates the old objects that the write barrier
     * remembered. They are allocated in the nursery, but promoted by the first
     * GC and never moved again. A frame can only be old after a GC, so storing
     * them into a frame never needs the write barrier. */
    inline void SetTopNoBarrier(vm_oop_t val) {
        assert(IS_TAGGED(val) || val == load_ptr(nilObject) ||
               val == load_ptr(trueObject) || val == load_ptr(falseObject));
        *stack_ptr = store_with_separate_barrier(val);
    }

    /* Only valid for tagged integers, nil, true, and false, see
     * SetTopNoBarrier(). */
    inline void PushNoBarrier(vm_oop_t obj) {
        assert(IS_TAGGED(obj) || obj == load_ptr(nilObject) ||
               obj == load_ptr(trueObject) || obj == load_ptr(falseObject));
        assert(RemainingStackSize() > 0);
        ++stack_ptr;
        SetTopNoBarrier(obj);
    }

    /* Stores into the current frame, which need no write barrier: the
     * interpreter remembers the frame when it stops being the current one,
     * and before a collection, see Interpreter::SetFrame(). */
    inline void PushIntoCurrent(vm_oop_t obj) {
        assert(IsCurrent());
        assert(RemainingStackSize() > 0);
        ++stack_ptr;
        *stack_ptr = store_with_separate_barrier(obj);
    }

    inline void SetContextOfCurrent(VMFrame* frm) {
        assert(IsCurrent());
        context = store_with_separate_barrier(frm);
    }

    inline void SetTopOfCurrent(vm_oop_t val) {
        assert(IsCurrent());
        *stack_ptr = store_with_separate_barrier(val);
    }

    inline void SetArgumentOfCurrent(size_t argIdx, vm_oop_t value) {
        assert(IsCurrent());
        arguments()[argIdx] = store_with_separate_barrier(value);
    }

    /** Whether this is the interpreter's current frame. */
    [[nodiscard]] bool IsCurrent() const;

#ifdef FRUCTUS
    [[nodiscard]] inline size_t GetBytecodeIndex() const {
        return (size_t)(bytecodePointer - GetMethod()->GetBytecodes());
    }

    [[nodiscard]] inline uint8_t* GetBytecodePointer() const {
        return bytecodePointer;
    }

    inline void SetBytecodePointer(uint8_t* pc) { bytecodePointer = pc; }
#else
    [[nodiscard]] inline size_t GetBytecodeIndex() const {
        return bytecodeIndex;
    }
#endif

    [[nodiscard]] inline vm_oop_t GetStackElement(size_t index) const {
        return load_ptr(stack_ptr[-std::make_signed_t<som_int_t>(index)]);
    }

    inline vm_oop_t GetLocal(uint8_t index, uint8_t contextLevel) {
        VMFrame const* context = GetContextLevel(contextLevel);
        return load_ptr(context->locals[index]);
    }

    inline vm_oop_t GetLocalInCurrentContext(uint8_t localIndex) {
        return load_ptr(this->locals[localIndex]);
    }

    void SetLocal(uint8_t index, uint8_t contextLevel, vm_oop_t value);

    inline void SetLocal(uint8_t index, vm_oop_t value) {
        store_ptr(locals[index], value);
    }

    inline vm_oop_t GetArgument(uint8_t index, uint8_t contextLevel) {
        // get the context
        VMFrame const* context = GetContextLevel(contextLevel);
        return load_ptr(context->arguments()[index]);
    }

    [[nodiscard]] inline vm_oop_t GetArgumentInCurrentContext(
        uint8_t index) const {
        return load_ptr(arguments()[index]);
    }

    void SetArgument(uint8_t index, uint8_t contextLevel, vm_oop_t value);
    void PrintStackTrace() const;
    void CopyArgumentsFrom(VMFrame* frame);

    inline void SetArgument(size_t argIdx, vm_oop_t value) {
        store_ptr(arguments()[argIdx], value);
    }

    void WalkObjects(walk_heap_fn /*unused*/) override;
    [[nodiscard]] VMFrame* CloneForMovingGC() const override;

#if DEBUG
    void PrintStack() const;
#endif
#ifndef FRUCTUS
    void PrintBytecode() const;
#endif

    [[nodiscard]] size_t RemainingStackSize() const {
        // - 1 because the stack pointer points at the top entry,
        // so the next entry would be put at stackPointer+1
        size_t const size =
            ((size_t)this + GetObjectSize() - size_t(stack_ptr)) /
            sizeof(VMObject*);
        return size - 1;
    }

    DECLARE_AS_DEBUG_STRING(override)

    make_testable(public);

#ifdef FRUCTUS
    // A pointer, where the host has an index, since the methods are in the
    // image and don't move. It's above 32K, so the frame stack's scan takes
    // it for a small integer or for a pointer out of the heap, see
    // Interpreter::WalkGlobals().
    uint8_t* bytecodePointer;
#else
    size_t bytecodeIndex{0};
    size_t totalObjectSize;
#endif

    void ResetStackPointer() {
        VMMethod const* meth = GetMethod();
        // Set the stack pointer to its initial value thereby clearing the stack
        stack_ptr = locals + meth->GetNumberOfLocals() - 1;
    }

    void ResetBytecodeIndex();

private:
    GCFrame* previousFrame;
    GCFrame* context{nullptr};
    GCMethod* method;
    gc_oop_t* locals;
    gc_oop_t* stack_ptr;

    /**
     * The arguments, then the locals and the stack, which come right after
     * the fields.
     */
    [[nodiscard]] gc_oop_t* arguments() const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
        return (gc_oop_t*)SHIFTED_PTR(this, sizeof(VMFrame));
    }

    static const size_t VMFrameNumberOfFields;

#ifdef FRUCTUS
    inline void SetBytecodeIndex(size_t index) {
        bytecodePointer = GetMethod()->GetBytecodes() + index;
    }
#else
    inline void SetBytecodeIndex(size_t index) { bytecodeIndex = index; }
#endif
};

bool VMFrame::HasContext() const {
    return context != nullptr;
}

bool VMFrame::HasPreviousFrame() const {
    return previousFrame != nullptr;
}

bool VMFrame::IsBootstrapFrame() const {
    return !HasPreviousFrame();
}

VMFrame* VMFrame::GetContext() const {
    return load_ptr(context);
}

VMFrame* VMFrame::GetContextLevel(size_t lvl) {
    VMFrame* current = this;
    while (lvl > 0) {
        current = current->GetContext();
        --lvl;
    }
    return current;
}

void VMFrame::SetContext(VMFrame* frm) {
    store_ptr(context, frm);
}

VMFrame* VMFrame::GetPreviousFrame() const {
    return load_ptr(previousFrame);
}

void VMFrame::ClearPreviousFrame() {
    previousFrame = nullptr;
}

VMMethod* VMFrame::GetMethod() const {
    return load_ptr(method);
}
