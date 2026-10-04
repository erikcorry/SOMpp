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
// of the running method, so that it can stay in a register. The frame's
// bytecode index is only up to date after SAVE_PC(): it must be saved before
// anything that may look at the frame, switch frames or move the method,
// and reloaded after anything that may have switched frames or moved it.
#define SAVE_PC() frame->SetBytecodeIndex(pc - method->GetBytecodes())

// Saves the index of the next bytecode, for a bytecode `bcCount` long.
#define SAVE_NEXT_PC(bcCount) \
    frame->SetBytecodeIndex(pc + (bcCount) - method->GetBytecodes())

#define RELOAD_PC() pc = method->GetBytecodes() + frame->GetBytecodeIndex()

#define DISPATCH_NOGC()         \
    {                           \
        goto* loopTargets[*pc]; \
    }

#define DISPATCH_GC()                                       \
    {                                                       \
        if (GetHeap<HEAP_CLS>()->isCollectionTriggered()) { \
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

#define NEXT_GC(bcCount) \
    {                    \
        pc += (bcCount); \
        DISPATCH_GC();   \
    }

class Interpreter {
    // The fields come first, and the most used first: fructus has short
    // encodings for the offsets 0, 2, 4, 6, and 8.
    VMFrame* frame;
    VMMethod* method;

public:
    vm_oop_t Start();

    VMFrame* PushNewFrame(VMMethod* method);
    void SetFrame(VMFrame* frm);

    /** Drop the frames, after Start() returned from the last one. */
    void ClearFrames() {
        frame = nullptr;
        method = nullptr;
    }

    inline VMFrame* GetFrame() { return frame; }

    VMMethod* GetMethod();
    uint8_t* GetBytecodes();
    void WalkGlobals(walk_heap_fn /*walk*/);

    void SendUnknownGlobal(VMSymbol* globalName);

private:
    vm_oop_t GetSelf();

    static constexpr const char* unknownGlobal = "unknownGlobal:";
    static constexpr const char* doesNotUnderstand =
        "doesNotUnderstand:arguments:";
    static constexpr const char* escapedBlock = "escapedBlock:";

    void startGC();

    VMFrame* popFrame();
    void popFrameAndPushResult(vm_oop_t result);

    void sendMiss(uint8_t* site, VMClass* receiverClass);

    void triggerDoesNotUnderstand(VMSymbol* signature);

    void doDup();
    void doPushLocal(uint8_t bc1, uint8_t bc2);
    void doPushLocalWithIndex(uint8_t localIndex);
    void doReturnFieldWithIndex(uint8_t fieldIndex);
    void doPushArgument(uint8_t argIndex, uint8_t contextLevel);
    void doPushField(uint8_t fieldIndex);
    void doPushFieldWithIndex(uint8_t fieldIndex);
    void doPushBlock(VMInvokable* blockMethod);
    bool doPushGlobal(VMSymbol* globalName);
    void doPop();
    void doPopLocal(uint8_t bc1, uint8_t bc2);
    void doPopLocalWithIndex(uint8_t localIndex);
    void doPopArgument(uint8_t bc1, uint8_t bc2);
    void doPopField(uint8_t fieldIndex);
    void doPopFieldWithIndex(uint8_t fieldIndex);
    void doSend(uint8_t* site);
    void doUnarySend(uint8_t* site);
    void doSuperSend(uint8_t* site);
    void doReturnLocal();
    void doReturnNonLocal();
    void doInc();
    void doDec();
    void doIncField(uint8_t fieldIndex);
    void doIncFieldPush(uint8_t fieldIndex);
    bool checkIsGreater();
};

/** There is one interpreter. It has no constructor, so it needs no static
 * initialization, and it starts out zeroed. */
extern Interpreter theInterpreter ZP;

inline Interpreter* GetInterpreter() {
    return &theInterpreter;
}
