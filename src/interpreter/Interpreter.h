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

#define DISPATCH_NOGC()                                           \
    {                                                             \
        goto* loopTargets[currentBytecodes[bytecodeIndexGlobal]]; \
    }

#define DISPATCH_GC()                                             \
    {                                                             \
        if (GetHeap<HEAP_CLS>()->isCollectionTriggered()) {       \
            startGC();                                            \
        }                                                         \
        goto* loopTargets[currentBytecodes[bytecodeIndexGlobal]]; \
    }

class Interpreter {
    // The fields come first, and the most used first: fructus has short
    // encodings for the offsets 0, 2, 4, 6, and 8.

    // The following two fields cache main parts of the current execution
    // context
    size_t bytecodeIndexGlobal;
    uint8_t* currentBytecodes;

    VMFrame* frame;
    VMMethod* method;

public:
    vm_oop_t Start();

    VMFrame* PushNewFrame(VMMethod* method);
    void SetFrame(VMFrame* frm);

    inline VMFrame* GetFrame() { return frame; }

    VMMethod* GetMethod();
    uint8_t* GetBytecodes();
    void WalkGlobals(walk_heap_fn /*walk*/);

    void SendUnknownGlobal(VMSymbol* globalName);

    inline size_t GetBytecodeIndex() { return bytecodeIndexGlobal; }

    void ResetBytecodeIndex(VMFrame* forFrame) {
        assert(frame == forFrame);
        assert(forFrame != nullptr);
        bytecodeIndexGlobal = 0;
    }

private:
    vm_oop_t GetSelf();

    static constexpr const char* unknownGlobal = "unknownGlobal:";
    static constexpr const char* doesNotUnderstand =
        "doesNotUnderstand:arguments:";
    static constexpr const char* escapedBlock = "escapedBlock:";

    void startGC();

    VMFrame* popFrame();
    void popFrameAndPushResult(vm_oop_t result);

    void send(VMSymbol* signature, VMClass* receiverClass);

    void triggerDoesNotUnderstand(VMSymbol* signature);

    void doDup();
    void doPushLocal(size_t bytecodeIndex);
    void doPushLocalWithIndex(uint8_t localIndex);
    void doReturnFieldWithIndex(uint8_t fieldIndex);
    void doPushArgument(size_t bytecodeIndex);
    void doPushField(size_t bytecodeIndex);
    void doPushFieldWithIndex(uint8_t fieldIndex);
    void doPushBlock(size_t bytecodeIndex);

    inline void doPushConstant(size_t bytecodeIndex) {
        vm_oop_t constant = method->GetConstant(bytecodeIndex);
        GetFrame()->Push(constant);
    }

    void doPushGlobal(size_t bytecodeIndex);
    void doPop();
    void doPopLocal(size_t bytecodeIndex);
    void doPopLocalWithIndex(uint8_t localIndex);
    void doPopArgument(size_t bytecodeIndex);
    void doPopField(size_t bytecodeIndex);
    void doPopFieldWithIndex(uint8_t fieldIndex);
    void doSend(size_t bytecodeIndex);
    void doUnarySend(size_t bytecodeIndex);
    void doSuperSend(size_t bytecodeIndex);
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
