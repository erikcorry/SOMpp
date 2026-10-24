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
// frame's bytecode index (the pc itself on fructus) and stack_ptr are only up
// to date after SAVE_PC(): it must be saved before anything that may look at
// the frame, switch frames or move the method, and reloaded after anything
// that may have switched frames or moved them.
#ifdef FRUCTUS
// the frames have the pc itself, since the methods don't move, see VMFrame.h
  #define SAVE_PC() (frame->stack_ptr = sp, frame->SetBytecodePointer(pc))

  // Saves the pc of the next bytecode, for a bytecode `bcCount` long.
  #define SAVE_NEXT_PC(bcCount) \
      (frame->stack_ptr = sp, frame->SetBytecodePointer(pc + (bcCount)))

  #define RELOAD_PC() (pc = frame->GetBytecodePointer(), sp = frame->stack_ptr)
#else
  #define SAVE_PC()           \
      (frame->stack_ptr = sp, \
       frame->SetBytecodeIndex(pc - method->GetBytecodes()))

  // Saves the index of the next bytecode, for a bytecode `bcCount` long.
  #define SAVE_NEXT_PC(bcCount) \
      (frame->stack_ptr = sp,   \
       frame->SetBytecodeIndex(pc + (bcCount) - method->GetBytecodes()))

  #define RELOAD_PC()                                           \
      (pc = method->GetBytecodes() + frame->GetBytecodeIndex(), \
       sp = frame->stack_ptr)
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
#if USE_FRAME_STACK
    // The top of the frame stack while the current frame is in the heap,
    // where the next frame on the stack goes. While the current frame is on
    // the stack, the top is its stack_ptr + 1, see FrameStackTop().
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

    VMFrame* PushNewFrame(VMMethod* method);
    void SetFrame(VMFrame* frm);

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

    /** Where the next frame on the frame stack goes. */
    [[nodiscard]] gc_oop_t* FrameStackTop() const {
        if (frame != nullptr && OnFrameStack(frame)) {
            return frame->stack_ptr + 1;
        }
        return stackTop;
    }

    /**
     * Room for a frame of `size` bytes at the top of the frame stack, or
     * nullptr after asking for a retry, as a failed allocation does.
     */
    void* AllocateStackFrame(size_t size) {
        gc_oop_t* const at = FrameStackTop();
        if ((char*)at + size > stackEnd && !growFrameStack((char*)at + size)) {
            return nullptr;
        }
        return at;
    }
#endif

    inline VMFrame* GetFrame() { return frame; }

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

    VMFrame* popFrame();
    void popFrameAndPushResult(vm_oop_t result);

    void sendMiss(uint8_t* site, VMClass* receiverClass);

#ifdef FRUCTUS
    [[noreturn]] void triggerDoesNotUnderstand(VMSymbol* signature,
                                               VMClass* cls);
#else
    void triggerDoesNotUnderstand(VMSymbol* signature);
#endif

    void doReturnFieldWithIndex(uint8_t fieldIndex);
    vm_oop_t getField(uint8_t fieldIndex);
    void setField(uint8_t fieldIndex, vm_oop_t o);
    void doSend(uint8_t* site);
    void doUnarySend(uint8_t* site);
#ifndef FRUCTUS
    void doSuperSend(uint8_t* site);
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
