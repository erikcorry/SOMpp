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

/**
 * A frame, of one of two kinds: a VMStackFrame on the frame stack, or a
 * VMHeapFrame in the heap, see ideas/frame-stack.md. The fields only a heap
 * frame needs are in VMHeapFrame, after the ones here, so a stack frame is
 * smaller.
 */
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
     * grow. With the sender's self, which a send saves above the arguments,
     * see Interpreter::PushSelf(): #doesNotUnderstand:arguments: needs 4
     * slots, where the send that failed had at least 2, #unknownGlobal: needs
     * 3, where the global had 1, and #escapedBlock: needs 3, where the send
     * of the block had at least 2, and so does the #error: that a unary
     * Vector primitive sends in place of its own send, see
     * VMSafePrimitive::SendError().
     */
    static constexpr size_t EXTRA_STACK_SLOTS = 2;
#endif

    [[nodiscard]] som_int_t GetHash() const final {
        return 0; /* should never be called */
    }

    [[nodiscard]] inline VMClass* GetClass() const final { return nullptr; }

    /** The slots of a frame of `method` after its arguments. */
    static size_t SlotsAfterArgumentsFor(const VMMethod* method) {
        return method->GetNumberOfLocals() +
               method->GetMaximumNumberOfStackElements() + EXTRA_STACK_SLOTS;
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

    /*
     * The stack of the current frame, for the C++ code outside the
     * interpreter's loop. Its top is where the interpreter keeps it, see
     * Interpreter::GetStackPointer(), so they're in Interpreter.h.
     */
    inline vm_oop_t Pop();
    inline void PopVoid();
    inline void Push(vm_oop_t obj);
    [[nodiscard]] inline vm_oop_t GetStackElement(size_t index) const;
    /** Empties the stack, see Block>>restart. */
    inline void ResetStackPointer();

    /* Stores into the current frame, which need no write barrier: the
     * interpreter remembers the frame when it stops being the current one,
     * and before a collection, see Interpreter::SetFrame(). */
    inline void PushIntoCurrent(vm_oop_t obj);

    /** The top of a new frame's stack, with only its locals. */
    [[nodiscard]] gc_oop_t* InitialStackPointer() const {
        return locals + GetMethod()->GetNumberOfLocals() - 1;
    }

    inline void SetContextOfCurrent(VMFrame* frm) {
        assert(IsCurrent());
        context = store_with_separate_barrier(frm);
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

    inline void SetArgument(size_t argIdx, vm_oop_t value) {
        store_ptr(arguments()[argIdx], value);
    }

    [[nodiscard]] VMFrame* CloneForMovingGC() const override = 0;

#if DEBUG
    void PrintStack() const;
#endif
#ifndef FRUCTUS
    void PrintBytecode() const;
#endif

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
#endif

    void ResetBytecodeIndex();

protected:
    /**
     * A frame whose locals start at `locals`. It's inlined into each kind's
     * constructor, as one constructor was before there were two kinds, see
     * Universe::NewFrame().
     */
    __attribute__((always_inline)) VMFrame(gc_oop_t* locals, VMMethod* method,
                                           VMFrame* previousFrame)
        :
#ifdef FRUCTUS
          bytecodePointer(method->GetBytecodes()),
#endif
          previousFrame(store_root(previousFrame)), method(store_root(method)),
          locals(locals) {
        // The invokable sees to the arguments, see VMMethod::Invoke(), and
        // the stack needs nothing,
        // since nothing above its top is a root, see WalkObjects() and
        // Interpreter::WalkGlobals().
        gc_oop_t* const end = locals + method->GetNumberOfLocals();
        for (gc_oop_t* local = locals; local < end; local++) {
            *local = nilObject;
        }
    }

    GCFrame* previousFrame;
    GCFrame* context{nullptr};
    GCMethod* method;
    gc_oop_t* locals;

    /**
     * The arguments, which are where the frame's kind has them, see
     * VMStackFrame and VMHeapFrame, so it's in Interpreter.h, which knows the
     * frame's kind.
     */
    [[nodiscard]] inline gc_oop_t* arguments() const;

    static const size_t VMFrameNumberOfFields;

#ifdef FRUCTUS
    inline void SetBytecodeIndex(size_t index) {
        bytecodePointer = GetMethod()->GetBytecodes() + index;
    }
#else
    inline void SetBytecodeIndex(size_t index) { bytecodeIndex = index; }
#endif
};

/**
 * A frame on the frame stack. The stack is scanned word by word, so a stack
 * frame is never walked, cloned, or asked its size by a collector.
 *
 * Its arguments stay where its sender pushed them, and the sender's self
 * above them, see Interpreter::PushSelf(): the frame goes right above, and
 * its locals and stack after its fields. A sender in the heap has its stack
 * there, so the arguments are copied below the frame, with a slot for the
 * self, see Universe::NewFrame(). The arguments are ArgumentGap() slots
 * further below the locals than in a heap frame, which the offsets of the
 * method's bytecodes allow for, see VMMethod::MoveArguments().
 *
 * It has no stack_ptr: the top of its stack is right below the frame above
 * it on the frame stack, or below the frame stack's top, if the frames above
 * it are in the heap, or in the interpreter while it's the current frame,
 * see Interpreter::GetStackPointer().
 */
class VMStackFrame final : public VMFrame {
public:
    VMStackFrame(VMMethod* method, VMFrame* previousFrame)
        : VMFrame((gc_oop_t*)SHIFTED_PTR(this, sizeof(VMStackFrame)), method,
                  previousFrame) {}

    /**
     * The size of a frame of `method` on the frame stack, without its
     * arguments.
     */
    static size_t SizeFor(const VMMethod* method) {
        return sizeof(VMStackFrame) +
               SlotsAfterArgumentsFor(method) * sizeof(gc_oop_t);
    }

    /** The slots between the arguments and the locals: the self, and the
     * frame's fields. */
    static constexpr size_t ArgumentGap();

    /** The method is alive and in place while its frame is on the stack. */
    [[nodiscard]] size_t GetObjectSize() const final {
        return GetMethod()->GetStackFrameSize();
    }

    [[nodiscard]] VMFrame* CloneForMovingGC() const final;
};

/** A frame in the heap, which may be a block's context. */
class VMHeapFrame final : public VMFrame {
public:
    VMHeapFrame(size_t additionalBytes, VMMethod* method,
                VMFrame* previousFrame)
        : VMFrame((gc_oop_t*)SHIFTED_PTR(this, sizeof(VMHeapFrame)) +
                      method->GetNumberOfArguments(),
                  method, previousFrame),
          stack_ptr(InitialStackPointer())
#ifndef FRUCTUS
          ,
          totalObjectSize(additionalBytes + sizeof(VMHeapFrame))
#endif
    {
#ifdef FRUCTUS
        // the size comes from the method, see GetObjectSize()
        (void)additionalBytes;
#endif
    }

    /**
     * The size of a frame of `method` in the heap: its arguments, then its
     * locals and its stack come after its fields.
     */
    static size_t SizeFor(const VMMethod* method) {
        return sizeof(VMHeapFrame) + (method->GetNumberOfArguments() +
                                      SlotsAfterArgumentsFor(method)) *
                                         sizeof(gc_oop_t);
    }

    /**
     * On fructus the size comes from the method, which is in the image and
     * never moves, so a frame needs no field for it. The host's methods are
     * in the heap, and a collector may move or free one while it needs the
     * size of a frame of it, so the frame has the size.
     */
    [[nodiscard]] size_t GetObjectSize() const final {
#ifdef FRUCTUS
        return SizeFor(GetMethod());
#else
        return totalObjectSize;
#endif
    }

    void WalkObjects(walk_heap_fn /*unused*/) final;
    [[nodiscard]] VMFrame* CloneForMovingGC() const final;

    /** The first argument, at the heap frame's offset, see arguments(). */
    [[nodiscard]] vm_oop_t GetSelf() const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
        return load_ptr(*(gc_oop_t*)SHIFTED_PTR(this, sizeof(VMHeapFrame)));
    }

    make_testable(public);

    /**
     * The top of the stack, which is up to date only where the interpreter
     * saved it, see Interpreter::GetStackPointer().
     */
    gc_oop_t* stack_ptr;
#ifndef FRUCTUS
    size_t totalObjectSize;
#endif
};

constexpr size_t VMStackFrame::ArgumentGap() {
    return 1 + sizeof(VMStackFrame) / sizeof(gc_oop_t);
}

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

// the operations on the current frame's stack, and arguments(), which need
// the interpreter
#include "../interpreter/Interpreter.h"
