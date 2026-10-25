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

#include <assert.h>

#include "../misc/defs.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMFrame.h"
#include "../vmobjects/VMMethod.h"

// The program counter, pc, is a local of Start() pointing into the bytecodes
// of the running method, and so is the stack pointer, sp, pointing at the top
// of the current frame's stack, so that they can stay in registers. The
// frame's bytecode index (the pc itself on fructus) and the top of its stack,
// see GetStackPointer(), are only up to date after SAVE_PC(): it must be
// saved before anything that may look at the frame, switch frames or move the
// method, and reloaded after anything that may have switched frames or moved
// them.
#ifdef FRUCTUS
// the frames have the pc itself, since the methods don't move, see VMFrame.h
  #define SAVE_PC() (SetStackPointer(sp), frame->SetBytecodePointer(pc))

  // Saves the pc of the next bytecode, for a bytecode `bcCount` long.
  #define SAVE_NEXT_PC(bcCount) \
      (SetStackPointer(sp), frame->SetBytecodePointer(pc + (bcCount)))

  #define RELOAD_PC() (pc = frame->GetBytecodePointer(), sp = GetStackPointer())
#else
  #define SAVE_PC()         \
      (SetStackPointer(sp), \
       frame->SetBytecodeIndex(pc - method->GetBytecodes()))

  // Saves the index of the next bytecode, for a bytecode `bcCount` long.
  #define SAVE_NEXT_PC(bcCount) \
      (SetStackPointer(sp),     \
       frame->SetBytecodeIndex(pc + (bcCount) - method->GetBytecodes()))

  #define RELOAD_PC()                                           \
      (pc = method->GetBytecodes() + frame->GetBytecodeIndex(), \
       sp = GetStackPointer())
#endif

// A send saves and reloads only the pc: the invokable gets sp, and answers
// the top of the current frame's stack, see VMInvokable::Invoke(). So does a
// return, which answers the top of the caller's stack, see
// popFrameAndPushResult().
#ifdef FRUCTUS
  #define SAVE_NEXT_PC_ONLY(bcCount) frame->SetBytecodePointer(pc + (bcCount))
  #define RELOAD_PC_ONLY() (pc = frame->GetBytecodePointer())
#else
  #define SAVE_NEXT_PC_ONLY(bcCount) \
      frame->SetBytecodeIndex(pc + (bcCount) - method->GetBytecodes())
  #define RELOAD_PC_ONLY() \
      (pc = method->GetBytecodes() + frame->GetBytecodeIndex())
#endif

#define DISPATCH_NOGC()         \
    {                           \
        goto* loopTargets[*pc]; \
    }

// Collects if a collection was requested, with pc at the next bytecode. If
// an allocation of the bytecode before failed, that bytecode changed nothing,
// and it runs again after the collection: it is `bcCount` long, and a send
// that failed pushed no frame, so it's the one before pc.
#define DISPATCH_GC(bcCount)                                \
    {                                                       \
        if (GetHeap<HEAP_CLS>()->isCollectionTriggered()) { \
            if (GetHeap<HEAP_CLS>()->isRetryRequested()) {  \
                pc -= (bcCount);                            \
            }                                               \
            SAVE_PC();                                      \
            startGC();                                      \
            RELOAD_PC();                                    \
        }                                                   \
        goto* loopTargets[*pc];                             \
    }

// A bytecode reads its operands at positive offsets from pc, then moves pc
// to the next bytecode as it dispatches. Fructus loads at positive offsets
// better than at negative ones.
#define NEXT_NOGC(bcCount) \
    {                      \
        pc += (bcCount);   \
        DISPATCH_NOGC();   \
    }

#define NEXT_GC(bcCount)      \
    {                         \
        pc += (bcCount);      \
        DISPATCH_GC(bcCount); \
    }

class Interpreter {
    // The fields come first, and the most used first: fructus has short
    // encodings for the offsets 0, 2, 4, 6, and 8.
    VMFrame* frame;
    VMMethod* method;
    // The receiver of the current context's method, see GetSelf(), so the
    // field bytecodes needn't find the arguments of the outer context.
    gc_oop_t self;
#if USE_FRAME_STACK
    // The top of the frame stack, where the next frame on it goes. While the
    // current frame is on the stack, it's the top of the frame's stack + 1,
    // where that was saved, see GetStackPointer(). A frame on the stack that
    // calls saves it there, and one that returns leaves its own address, see
    // popFrame().
    gc_oop_t* stackTop;
    // The frame stack's end, and its base on the host, copied from the heap
    // for the frame switches, see SlidingHeap.h.
    char* stackEnd;
  #ifndef FRUCTUS
    char* stackBase;
  #endif
#endif

public:
    vm_oop_t Start();

    /** Makes a frame of method the current one, whose self the caller sets. */
    VMFrame* PushNewFrame(VMMethod* method);
    void SetFrame(VMFrame* frm);

    /** Sets the current context's self, for a new frame, see GetSelf(). */
    void SetSelf(vm_oop_t receiver) { self = store_root(receiver); }

    /**
     * Saves self above the top of the current frame's stack, at `sp`, for a
     * send that pushes a frame, and answers the new top. The frame's return
     * restores it, see popFrameAndPushResult(). The compiler counts the slot
     * in the sender's stack, see MethodGenerationContext::AddSend().
     */
    gc_oop_t* PushSelf(gc_oop_t* sp) {
        // the current frame needs no write barrier, see switchFrame()
        sp[1] = self;
        return sp + 1;
    }

    /** The self of `frm`: the first argument of its outer context. */
    static vm_oop_t SelfOf(VMFrame* frm);

    /**
     * Drop the frames, after Start() returned from the last one, or before
     * the first.
     */
    void ClearFrames();

#if USE_FRAME_STACK
    /** Whether `p` is on the frame stack, see ideas/frame-stack.md. */
    [[nodiscard]] bool OnFrameStack(const void* p) const {
  #ifdef FRUCTUS
        // below the stack's end are the stack and things that aren't frames
        return p < (const void*)stackEnd;
  #else
        return p >= (const void*)stackBase && p < (const void*)stackEnd;
  #endif
    }

    /**
     * Room for a frame of `size` bytes at the top of the frame stack, or
     * nullptr after asking for a retry, as a failed allocation does.
     */
    void* AllocateStackFrame(size_t size) {
        gc_oop_t* const at = stackTop;
        if ((char*)at + size > stackEnd && !growFrameStack((char*)at + size)) {
            return nullptr;
        }
        return at;
    }
#endif

    inline VMFrame* GetFrame() { return frame; }

    /** Whether `frm` is a VMStackFrame, on the frame stack. */
    [[nodiscard]] bool IsStackFrame(const VMFrame* frm) const {
#if USE_FRAME_STACK
        return OnFrameStack(frm);
#else
        (void)frm;
        return false;
#endif
    }

    /**
     * The top of the current frame's stack, where it was saved, see
     * SAVE_PC() and VMInvokable::InvokeFromVM(): the frame stack's top
     * for a frame on it, see VMStackFrame, or the frame's stack_ptr.
     */
    [[nodiscard]] gc_oop_t* GetStackPointer() const {
#if USE_FRAME_STACK
        if (OnFrameStack(frame)) {
            return stackTop - 1;
        }
#endif
        return static_cast<VMHeapFrame*>(frame)->stack_ptr;
    }

    void SetStackPointer(gc_oop_t* sp) {
#if USE_FRAME_STACK
        if (OnFrameStack(frame)) {
            stackTop = sp + 1;
            return;
        }
#endif
        static_cast<VMHeapFrame*>(frame)->stack_ptr = sp;
    }

    /**
     * Whether the slot at `p` is in the current frame, and on the frame
     * stack, below its end: the stack's end is where the deepest stack so
     * far ended, so a push past it is a frame that overflowed its stack.
     */
    [[nodiscard]] bool FitsInFrame(const gc_oop_t* p) const {
        if ((const char*)(p + 1) >
            (const char*)frame + frame->GetObjectSize()) {
            return false;
        }
#if USE_FRAME_STACK
        if (OnFrameStack(frame) && (const char*)(p + 1) > stackEnd) {
            return false;
        }
#endif
        return true;
    }

    VMMethod* GetMethod();
    uint8_t* GetBytecodes();
    void WalkGlobals(walk_heap_fn /*walk*/);

    /**
     * Sends #unknownGlobal: to self. Answers false, and changes nothing, if
     * it failed.
     */
    bool SendUnknownGlobal(VMSymbol* globalName);

#ifdef FRUCTUS
    /**
     * The error of a send of `selector`, a name, that an instance of cls
     * doesn't understand, for primitives that refuse some receivers.
     */
    [[noreturn]] void DoesNotUnderstand(const char* selector, VMClass* cls);
#endif

private:
    vm_oop_t GetSelf();

    static constexpr const char* unknownGlobal = "unknownGlobal:";
    static constexpr const char* doesNotUnderstand =
        "doesNotUnderstand:arguments:";
    static constexpr const char* escapedBlock = "escapedBlock:";

    void startGC();
#if USE_FRAME_STACK
    bool growFrameStack(char* end);
#endif

    void switchFrame(VMFrame* frm);
    VMFrame* popFrame();
    gc_oop_t* popFrameAndPushResult(vm_oop_t result);

    gc_oop_t* sendMiss(uint8_t* site, VMClass* receiverClass, gc_oop_t* sp);

#ifdef FRUCTUS
    [[noreturn]] void triggerDoesNotUnderstand(VMSymbol* signature,
                                               VMClass* cls);
#else
    void triggerDoesNotUnderstand(VMSymbol* signature);
#endif

    gc_oop_t* doReturnFieldWithIndex(uint8_t fieldIndex);
    vm_oop_t getField(uint8_t fieldIndex);
    void setField(uint8_t fieldIndex, vm_oop_t o);
    gc_oop_t* doSend(uint8_t* site, gc_oop_t* sp);
    gc_oop_t* doUnarySend(uint8_t* site, gc_oop_t* sp);
#ifndef FRUCTUS
    gc_oop_t* doSuperSend(uint8_t* site, gc_oop_t* sp);
#endif
    bool doReturnNonLocal();
#ifndef FRUCTUS
    bool sendEscapedBlock(vm_oop_t result);
#endif
    bool incTop(gc_oop_t* sp, som_int_t delta);
    vm_oop_t incField(uint8_t fieldIndex);
    static bool checkIsGreater(vm_oop_t top, vm_oop_t top2);
};

/** There is one interpreter. It has no constructor, so it needs no static
 * initialization, and it starts out zeroed. */
extern Interpreter theInterpreter ZP;

inline Interpreter* GetInterpreter() {
    return &theInterpreter;
}

gc_oop_t* VMFrame::arguments() const {
    auto const headerSize = GetInterpreter()->IsStackFrame(this)
                                ? sizeof(VMStackFrame)
                                : sizeof(VMHeapFrame);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    return (gc_oop_t*)SHIFTED_PTR(this, headerSize);
}

vm_oop_t VMFrame::Pop() {
    assert(IsCurrent());
    gc_oop_t* const sp = GetInterpreter()->GetStackPointer();
    GetInterpreter()->SetStackPointer(sp - 1);
    return load_ptr(*sp);
}

void VMFrame::PopVoid() {
    assert(IsCurrent());
    GetInterpreter()->SetStackPointer(GetInterpreter()->GetStackPointer() - 1);
}

void VMFrame::Push(vm_oop_t obj) {
    assert(IsCurrent());
    gc_oop_t* const sp = GetInterpreter()->GetStackPointer() + 1;
    assert(GetInterpreter()->FitsInFrame(sp));
    store_ptr(*sp, obj);
    GetInterpreter()->SetStackPointer(sp);
}

void VMFrame::PushIntoCurrent(vm_oop_t obj) {
    assert(IsCurrent());
    gc_oop_t* const sp = GetInterpreter()->GetStackPointer() + 1;
    assert(GetInterpreter()->FitsInFrame(sp));
    *sp = store_with_separate_barrier(obj);
    GetInterpreter()->SetStackPointer(sp);
}

vm_oop_t VMFrame::GetStackElement(size_t index) const {
    assert(IsCurrent());
    return load_ptr(
        GetInterpreter()
            ->GetStackPointer()[-std::make_signed_t<som_int_t>(index)]);
}

void VMFrame::ResetStackPointer() {
    assert(IsCurrent());
    GetInterpreter()->SetStackPointer(InitialStackPointer());
}
