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

#include "Interpreter.h"

#include <assert.h>
#include <cstddef>
#include <cstdint>
#ifndef FRUCTUS
#include <vector>
#endif

#include "../interpreter/SendSite.h"
#include "../interpreter/bytecodes.h"  // NOLINT(misc-include-cleaner) it's required for InterpreterLoop.h
#include "../memory/Heap.h"
#include "../misc/defs.h"
#include "../vm/Globals.h"
#include "../vm/IsValidObject.h"
#include "../vm/Print.h"
#include "../vm/Statistics.h"
#ifdef FRUCTUS
  #include "../vm/FructusImage.h"
#else
  #include "../vm/Symbols.h"
#endif
#include "../vm/Universe.h"
#include "../vmobjects/IntegerBox.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/Signature.h"
#include "../vmobjects/VMArray.h"
#ifndef FRUCTUS
#include "../vmobjects/VMBigInteger.h"  // NOLINT(misc-include-cleaner)
#endif
#include "../vmobjects/VMBlock.h"
#include "../vmobjects/VMClass.h"
#include "../vmobjects/VMDouble.h"
#include "../vmobjects/VMFrame.h"
#include "../vmobjects/VMInteger.h"
#include "../vmobjects/VMInvokable.h"
#include "../vmobjects/VMMethod.h"
#include "../vmobjects/VMObject.h"
#include "../vmobjects/VMSymbol.h"

Interpreter theInterpreter ZP;

vm_oop_t Interpreter::Start() {
#ifdef BYTECODE_HEATMAP
  #define HEATMAP_INC() method->heatmap[pc - method->GetBytecodes()]++
#else
  #define HEATMAP_INC() ((void)0)
#endif
#define PROLOGUE() HEATMAP_INC()

    // initialization
    method = GetMethod();
    uint8_t* pc = nullptr;
    RELOAD_PC();

    void const* const loopTargets[] = {&&LABEL_BC_HALT,
                                       &&LABEL_BC_DUP,
                                       &&LABEL_BC_DUP_SECOND,
                                       &&LABEL_BC_PUSH_LOCAL,
                                       &&LABEL_BC_PUSH_LOCAL_0,
                                       &&LABEL_BC_PUSH_LOCAL_1,
                                       &&LABEL_BC_PUSH_LOCAL_2,
                                       &&LABEL_BC_PUSH_ARGUMENT,
                                       &&LABEL_BC_PUSH_SELF,
                                       &&LABEL_BC_PUSH_ARG_1,
                                       &&LABEL_BC_PUSH_ARG_2,
                                       &&LABEL_BC_PUSH_FIELD,
                                       &&LABEL_BC_PUSH_FIELD_0,
                                       &&LABEL_BC_PUSH_FIELD_1,
                                       &&LABEL_BC_PUSH_BLOCK,
                                       &&LABEL_BC_PUSH_CONSTANT,
                                       &&LABEL_BC_PUSH_0,
                                       &&LABEL_BC_PUSH_1,
                                       &&LABEL_BC_PUSH_SMALL_INT,
                                       &&LABEL_BC_PUSH_NIL,
                                       &&LABEL_BC_PUSH_GLOBAL,
                                       &&LABEL_BC_POP,
                                       &&LABEL_BC_POP_LOCAL,
                                       &&LABEL_BC_POP_LOCAL_0,
                                       &&LABEL_BC_POP_LOCAL_1,
                                       &&LABEL_BC_POP_LOCAL_2,
                                       &&LABEL_BC_POP_ARGUMENT,
                                       &&LABEL_BC_POP_FIELD,
                                       &&LABEL_BC_POP_FIELD_0,
                                       &&LABEL_BC_POP_FIELD_1,
                                       &&LABEL_BC_SEND,
                                       &&LABEL_BC_SEND_1,
                                       &&LABEL_BC_SUPER_SEND,
                                       &&LABEL_BC_RETURN_LOCAL,
                                       &&LABEL_BC_RETURN_NON_LOCAL,
                                       &&LABEL_BC_RETURN_SELF,
                                       &&LABEL_BC_RETURN_FIELD_0,
                                       &&LABEL_BC_RETURN_FIELD_1,
                                       &&LABEL_BC_RETURN_FIELD_2,
                                       &&LABEL_BC_INC,
                                       &&LABEL_BC_DEC,
                                       &&LABEL_BC_INC_FIELD,
                                       &&LABEL_BC_INC_FIELD_PUSH,
                                       &&LABEL_BC_JUMP,
                                       &&LABEL_BC_JUMP_ON_FALSE_POP,
                                       &&LABEL_BC_JUMP_ON_TRUE_POP,
                                       &&LABEL_BC_JUMP_ON_FALSE_TOP_NIL,
                                       &&LABEL_BC_JUMP_ON_TRUE_TOP_NIL,
                                       &&LABEL_BC_JUMP_ON_NOT_NIL_POP,
                                       &&LABEL_BC_JUMP_ON_NIL_POP,
                                       &&LABEL_BC_JUMP_ON_NOT_NIL_TOP_TOP,
                                       &&LABEL_BC_JUMP_ON_NIL_TOP_TOP,
                                       &&LABEL_BC_JUMP_IF_GREATER,
                                       &&LABEL_BC_JUMP_BACKWARD,
                                       &&LABEL_BC_JUMP2,
                                       &&LABEL_BC_JUMP2_ON_FALSE_POP,
                                       &&LABEL_BC_JUMP2_ON_TRUE_POP,
                                       &&LABEL_BC_JUMP2_ON_FALSE_TOP_NIL,
                                       &&LABEL_BC_JUMP2_ON_TRUE_TOP_NIL,
                                       &&LABEL_BC_JUMP2_ON_NOT_NIL_POP,
                                       &&LABEL_BC_JUMP2_ON_NIL_POP,
                                       &&LABEL_BC_JUMP2_ON_NOT_NIL_TOP_TOP,
                                       &&LABEL_BC_JUMP2_ON_NIL_TOP_TOP,
                                       &&LABEL_BC_JUMP2_IF_GREATER,
                                       &&LABEL_BC_JUMP2_BACKWARD};

    goto* loopTargets[*pc];

    //
    // THIS IS THE former interpretation loop
LABEL_BC_HALT:
    PROLOGUE();
    SAVE_NEXT_PC(1);
    return GetFrame()->GetStackElement(0);  // handle the halt bytecode

LABEL_BC_DUP:
    PROLOGUE();
    doDup();
    NEXT_NOGC(1);

LABEL_BC_DUP_SECOND:
    PROLOGUE();
    {
        vm_oop_t elem = GetFrame()->GetStackElement(1);
        GetFrame()->Push(elem);
    }
    NEXT_NOGC(1);

LABEL_BC_PUSH_LOCAL:
    PROLOGUE();
    doPushLocal(pc[1], pc[2]);
    NEXT_NOGC(3);

LABEL_BC_PUSH_LOCAL_0:
    PROLOGUE();
    doPushLocalWithIndex(0);
    NEXT_NOGC(1);

LABEL_BC_PUSH_LOCAL_1:
    PROLOGUE();
    doPushLocalWithIndex(1);
    NEXT_NOGC(1);

LABEL_BC_PUSH_LOCAL_2:
    PROLOGUE();
    doPushLocalWithIndex(2);
    NEXT_NOGC(1);

LABEL_BC_PUSH_ARGUMENT:
    PROLOGUE();
    doPushArgument(pc[1], pc[2]);
    NEXT_NOGC(3);

LABEL_BC_PUSH_SELF:
    PROLOGUE();
    {
        vm_oop_t argument = GetFrame()->GetArgumentInCurrentContext(0);
        GetFrame()->Push(argument);
    }
    NEXT_NOGC(1);

LABEL_BC_PUSH_ARG_1:
    PROLOGUE();
    {
        vm_oop_t argument = GetFrame()->GetArgumentInCurrentContext(1);
        GetFrame()->Push(argument);
    }
    NEXT_NOGC(1);

LABEL_BC_PUSH_ARG_2:
    PROLOGUE();
    {
        vm_oop_t argument = GetFrame()->GetArgumentInCurrentContext(2);
        GetFrame()->Push(argument);
    }
    NEXT_NOGC(1);

LABEL_BC_PUSH_FIELD:
    PROLOGUE();
    doPushField(pc[1]);
    NEXT_NOGC(2);

LABEL_BC_PUSH_FIELD_0:
    PROLOGUE();
    doPushFieldWithIndex(0);
    NEXT_NOGC(1);

LABEL_BC_PUSH_FIELD_1:
    PROLOGUE();
    doPushFieldWithIndex(1);
    NEXT_NOGC(1);

LABEL_BC_PUSH_BLOCK:
    PROLOGUE();
    doPushBlock((VMInvokable*)LoadWord(pc + 1));
    NEXT_GC(1 + sizeof(void*));

LABEL_BC_PUSH_CONSTANT:
    PROLOGUE();
    GetFrame()->Push((vm_oop_t)LoadWord(pc + 1));
    NEXT_NOGC(1 + sizeof(void*));

LABEL_BC_PUSH_0:
    PROLOGUE();
    if (USE_TAGGING) {
        GetFrame()->PushNoBarrier(NEW_INT(0));
    } else {
        GetFrame()->Push(NEW_INT(0));
    }
    NEXT_NOGC(1);

LABEL_BC_PUSH_1:
    PROLOGUE();
    if (USE_TAGGING) {
        GetFrame()->PushNoBarrier(NEW_INT(1));
    } else {
        GetFrame()->Push(NEW_INT(1));
    }
    NEXT_NOGC(1);

LABEL_BC_PUSH_SMALL_INT:
    PROLOGUE();
#if USE_TAGGING
    GetFrame()->PushNoBarrier(NEW_INT((int8_t)pc[1]));
#else
    GetFrame()->Push(NEW_INT((int8_t)pc[1]));
#endif
    NEXT_NOGC(2);

LABEL_BC_PUSH_NIL:
    PROLOGUE();
    GetFrame()->PushNoBarrier(load_ptr(nilObject));
    NEXT_NOGC(1);

LABEL_BC_PUSH_GLOBAL:
    PROLOGUE();
    {
        auto* globalName = (VMSymbol*)LoadWord(pc + 1);
        pc += 1 + sizeof(void*);
        if (!doPushGlobal(globalName)) {
            // sends #unknownGlobal:, which pushes a new frame
            SAVE_PC();
            SendUnknownGlobal(globalName);
            RELOAD_PC();
        }
    }
    DISPATCH_GC(1 + sizeof(void*));

LABEL_BC_POP:
    PROLOGUE();
    doPop();
    NEXT_NOGC(1);

LABEL_BC_POP_LOCAL:
    PROLOGUE();
    doPopLocal(pc[1], pc[2]);
    NEXT_NOGC(3);

LABEL_BC_POP_LOCAL_0:
    PROLOGUE();
    doPopLocalWithIndex(0);
    NEXT_NOGC(1);

LABEL_BC_POP_LOCAL_1:
    PROLOGUE();
    doPopLocalWithIndex(1);
    NEXT_NOGC(1);

LABEL_BC_POP_LOCAL_2:
    PROLOGUE();
    doPopLocalWithIndex(2);
    NEXT_NOGC(1);

LABEL_BC_POP_ARGUMENT:
    PROLOGUE();
    doPopArgument(pc[1], pc[2]);
    NEXT_NOGC(3);

LABEL_BC_POP_FIELD:
    PROLOGUE();
    doPopField(pc[1]);
    NEXT_NOGC(2);

LABEL_BC_POP_FIELD_0:
    PROLOGUE();
    doPopFieldWithIndex(0);
    NEXT_NOGC(1);

LABEL_BC_POP_FIELD_1:
    PROLOGUE();
    doPopFieldWithIndex(1);
    NEXT_NOGC(1);

LABEL_BC_SEND:
    PROLOGUE();
    SAVE_NEXT_PC(SendSite::Length(BC_SEND));
    doSend(pc);
    RELOAD_PC();
    DISPATCH_GC(SendSite::Length(BC_SEND));

LABEL_BC_SEND_1:
    PROLOGUE();
    SAVE_NEXT_PC(SendSite::Length(BC_SEND_1));
    doUnarySend(pc);
    RELOAD_PC();
    DISPATCH_GC(SendSite::Length(BC_SEND_1));

LABEL_BC_SUPER_SEND:
    PROLOGUE();
    SAVE_NEXT_PC(SendSite::Length(BC_SUPER_SEND));
    doSuperSend(pc);
    RELOAD_PC();
    DISPATCH_GC(SendSite::Length(BC_SUPER_SEND));

LABEL_BC_RETURN_LOCAL:
    PROLOGUE();
    doReturnLocal();
    RELOAD_PC();
    DISPATCH_NOGC();

LABEL_BC_RETURN_NON_LOCAL:
    PROLOGUE();
#ifdef FRUCTUS
    doReturnNonLocal();  // an escaped block is an error, so it can't fail
#else
    if (!doReturnNonLocal()) {
        goto RETRY;
    }
#endif
    RELOAD_PC();
    DISPATCH_NOGC();

LABEL_BC_RETURN_SELF: {
    PROLOGUE();
    assert(GetFrame()->GetContext() == nullptr &&
           "RETURN_SELF is not allowed in blocks");
    popFrameAndPushResult(GetFrame()->GetArgumentInCurrentContext(0));
    RELOAD_PC();
    DISPATCH_NOGC();
}

LABEL_BC_RETURN_FIELD_0:
    PROLOGUE();
    doReturnFieldWithIndex(0);
    RELOAD_PC();
    DISPATCH_NOGC();

LABEL_BC_RETURN_FIELD_1:
    PROLOGUE();
    doReturnFieldWithIndex(1);
    RELOAD_PC();
    DISPATCH_NOGC();

LABEL_BC_RETURN_FIELD_2:
    PROLOGUE();
    doReturnFieldWithIndex(2);
    RELOAD_PC();
    DISPATCH_NOGC();

LABEL_BC_INC:
    PROLOGUE();
    if (!doInc()) {
        goto RETRY;
    }
    if (USE_TAGGING) {
        NEXT_NOGC(1);
    } else {
        // without integer tagging doInc() allocates memory and the IfNil
        // benchmark will allocate, but not reach a GC point, and run out of
        // memory
        NEXT_GC(1);
    }

LABEL_BC_DEC:
    PROLOGUE();
    if (!doDec()) {
        goto RETRY;
    }
    NEXT_NOGC(1);

LABEL_BC_INC_FIELD:
    PROLOGUE();
    if (!doIncField(pc[1])) {
        goto RETRY;
    }
    NEXT_NOGC(2);

LABEL_BC_INC_FIELD_PUSH:
    PROLOGUE();
    if (!doIncFieldPush(pc[1])) {
        goto RETRY;
    }
    NEXT_NOGC(2);

LABEL_BC_JUMP: {
    uint8_t const offset = pc[1];
    pc += offset;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_FALSE_POP: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(falseObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
    } else {
        pc += 3;
    }
    GetFrame()->PopVoid();
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_TRUE_POP: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(trueObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
    } else {
        pc += 3;
    }
    GetFrame()->PopVoid();
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_FALSE_TOP_NIL: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(falseObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
        GetFrame()->SetTopNoBarrier(load_ptr(nilObject));
    } else {
        GetFrame()->PopVoid();
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_TRUE_TOP_NIL: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(trueObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
        GetFrame()->SetTopNoBarrier(load_ptr(nilObject));
    } else {
        GetFrame()->PopVoid();
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_NOT_NIL_POP: {
    vm_oop_t val = GetFrame()->Top();
    if (val != load_ptr(nilObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
    } else {
        pc += 3;
    }
    GetFrame()->PopVoid();
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_NIL_POP: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(nilObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
    } else {
        pc += 3;
    }
    GetFrame()->PopVoid();
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_NOT_NIL_TOP_TOP: {
    vm_oop_t val = GetFrame()->Top();
    if (val != load_ptr(nilObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
        // GetFrame()->SetTop(val);
    } else {
        GetFrame()->PopVoid();
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_NIL_TOP_TOP: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(nilObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
        // GetFrame()->SetTop(val);
    } else {
        GetFrame()->PopVoid();
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_IF_GREATER: {
    if (checkIsGreater()) {
        pc += pc[1];
        GetFrame()->Pop();
        GetFrame()->Pop();
    } else {
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_BACKWARD: {
    uint8_t const offset = pc[1];
    pc -= offset;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2: {
    uint16_t const offset =
        ComputeOffset(pc[1],
                      pc[2]);
    pc += offset;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_FALSE_POP: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(falseObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
    } else {
        pc += 3;
    }
    GetFrame()->PopVoid();
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_TRUE_POP: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(trueObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
    } else {
        pc += 3;
    }
    GetFrame()->PopVoid();
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_FALSE_TOP_NIL: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(falseObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
        GetFrame()->SetTopNoBarrier(load_ptr(nilObject));
    } else {
        GetFrame()->PopVoid();
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_TRUE_TOP_NIL: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(trueObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
        GetFrame()->SetTopNoBarrier(load_ptr(nilObject));
    } else {
        GetFrame()->PopVoid();
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_NOT_NIL_POP: {
    vm_oop_t val = GetFrame()->Top();
    if (val != load_ptr(nilObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
    } else {
        pc += 3;
    }
    GetFrame()->PopVoid();
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_NIL_POP: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(nilObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
    } else {
        pc += 3;
    }
    GetFrame()->PopVoid();
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_NOT_NIL_TOP_TOP: {
    vm_oop_t val = GetFrame()->Top();
    if (val != load_ptr(nilObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
        // GetFrame()->SetTop(val);
    } else {
        GetFrame()->PopVoid();
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_NIL_TOP_TOP: {
    vm_oop_t val = GetFrame()->Top();
    if (val == load_ptr(nilObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
        // GetFrame()->SetTop(val);
    } else {
        GetFrame()->PopVoid();
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_IF_GREATER: {
    if (checkIsGreater()) {
        pc +=
            ComputeOffset(pc[1],
                          pc[2]);
        GetFrame()->Pop();
        GetFrame()->Pop();
    } else {
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_BACKWARD: {
    uint16_t const offset =
        ComputeOffset(pc[1],
                      pc[2]);
    pc -= offset;
}
    DISPATCH_NOGC();

    // An allocation of the bytecode at pc failed, and it changed nothing:
    // collect, and run it again. The bytecodes that go to DISPATCH_GC do this
    // there instead, since their pc has moved on.
RETRY:
    SAVE_PC();
    startGC();
    RELOAD_PC();
    DISPATCH_NOGC();
}

// Answers nullptr, and leaves the current frame, if the frame can't be
// allocated.
VMFrame* Interpreter::PushNewFrame(VMMethod* method) {
    VMFrame* frm = Universe::NewFrame(GetFrame(), method);
    if (frm != nullptr) {
        SetFrame(frm);
    }
    return frm;
}

// Doesn't save or restore the bytecode index: Start() saves it before
// anything that can switch frames, and reloads it afterwards.
void Interpreter::SetFrame(VMFrame* frm) {
    frame = frm;
    method = frm->GetMethod();
}

vm_oop_t Interpreter::GetSelf() {
    VMFrame const* const context = GetFrame()->GetOuterContext();
    return context->GetArgumentInCurrentContext(0);
}

VMFrame* Interpreter::popFrame() {
    VMFrame* result = GetFrame();
    SetFrame(GetFrame()->GetPreviousFrame());

    result->ClearPreviousFrame();

#ifdef UNSAFE_FRAME_OPTIMIZATION
    // remember this frame as free frame
    result->GetMethod()->SetCachedFrame(result);
#endif
    return result;
}

void Interpreter::popFrameAndPushResult(vm_oop_t result) {
    VMFrame const* const prevFrame = popFrame();

    VMMethod const* const method = prevFrame->GetMethod();
    uint8_t const numberOfArgs = method->GetNumberOfArguments();

    for (uint8_t i = 0; i < numberOfArgs; ++i) {
        GetFrame()->Pop();
    }

    GetFrame()->Push(result);
}

// The slow path of a send, which looks the selector up, and links the send
// if it's unlinked, see SendSite.h.
void Interpreter::sendMiss(uint8_t* site, VMClass* receiverClass) {
    bool const isUnlinked = SendSite::GetReceiverClass(site) == nullptr;
    VMSymbol* selector = method->GetSendSelector(site - method->GetBytecodes());
    VMInvokable* invokable = receiverClass->LookupInvokable(selector);

    if (invokable == nullptr) {
#ifdef FRUCTUS
        triggerDoesNotUnderstand(selector, receiverClass);
#else
        triggerDoesNotUnderstand(selector);
        return;
#endif
    }
    if (isUnlinked) {
        SendSite::Link(site, invokable, receiverClass);
#ifndef FRUCTUS
        // On fructus the classes and invokables are all in the image, which
        // the collector doesn't move or free, and the methods are above 32K,
        // which the cards don't cover.
        write_barrier(method, invokable);
        write_barrier(method, receiverClass);
#endif
    }
    recordStat(CallStats, receiverClass, invokable);
    if (site[0] == BC_SEND_1) {
        invokable->Invoke1(GetFrame());
    } else {
        invokable->Invoke(GetFrame());
    }
}

#ifdef FRUCTUS
// a closed world: the program can't have the selector, so it's an error, and
// doesNotUnderstand:arguments: is never sent. Selectors and classes have no
// names, so this is in the form tools/fructus-trace translates.
void Interpreter::triggerDoesNotUnderstand(VMSymbol* signature, VMClass* cls) {
    char selector[16];
    FormatTagged(selector, "sel", (uintptr_t)signature);
    DoesNotUnderstand(selector, cls);
}

void Interpreter::DoesNotUnderstand(const char* selector, VMClass* cls) {
    char className[16];
    FormatTagged(className, "class", (uintptr_t)cls);
    ErrorPrintf("ERROR: %s not understood by an instance of %s\n", selector,
                className);
    GetFrame()->PrintStackTrace();
    Quit(ERR_FAIL);
}
#else
// Sends #doesNotUnderstand:arguments: in place of the send of signature,
// whose receiver and arguments are on the stack. If it fails, they are put
// back, so the send can run again.
void Interpreter::triggerDoesNotUnderstand(VMSymbol* signature) {
    uint8_t const numberOfArgs = Signature::GetNumberOfArguments(signature);

    vm_oop_t receiver = GetFrame()->GetStackElement(numberOfArgs - 1);

    VMSymbol* selector = SymbolFor(doesNotUnderstand);
    VMArray* argumentsArray =
        Universe::NewArray(numberOfArgs - 1);  // without receiver
    if (selector == nullptr || argumentsArray == nullptr) {
        return;
    }

    // the receiver should not go into the argumentsArray
    // so, numberOfArgs - 2
    for (som_int_t i = numberOfArgs - 2; i >= 0; --i) {
        vm_oop_t o = GetFrame()->Pop();
        argumentsArray->SetIndexableField(i, o);
    }
    vm_oop_t arguments[] = {signature, argumentsArray};

    GetFrame()->Pop();  // pop the receiver

    // it needs 3 slots, and the failed send had at least 1, see
    // VMFrame::EXTRA_STACK_SLOTS
    if (!AS_OBJ(receiver)->Send(selector, arguments, 2)) {
        GetFrame()->Push(receiver);
        for (size_t i = 0; i < (size_t)numberOfArgs - 1; ++i) {
            GetFrame()->Push(argumentsArray->GetIndexableField(i));
        }
    }
}
#endif

void Interpreter::doDup() {
    vm_oop_t elem = GetFrame()->GetStackElement(0);
    GetFrame()->Push(elem);
}

void Interpreter::doPushLocal(uint8_t bc1, uint8_t bc2) {
    assert((bc1 != 0 || bc2 != 0) && "should have been BC_PUSH_LOCAL_0");
    assert((bc1 != 1 || bc2 != 0) && "should have been BC_PUSH_LOCAL_1");
    assert((bc1 != 2 || bc2 != 0) && "should have been BC_PUSH_LOCAL_2");

    vm_oop_t local = GetFrame()->GetLocal(bc1, bc2);

    GetFrame()->Push(local);
}

void Interpreter::doPushLocalWithIndex(uint8_t localIndex) {
    vm_oop_t local = GetFrame()->GetLocalInCurrentContext(localIndex);
    GetFrame()->Push(local);
}

void Interpreter::doPushArgument(uint8_t argIndex, uint8_t contextLevel) {
    assert((argIndex != 0 || contextLevel != 0) &&
           "should have been BC_PUSH_SELF");
    assert((argIndex != 1 || contextLevel != 0) &&
           "should have been BC_PUSH_ARG_1");
    assert((argIndex != 2 || contextLevel != 0) &&
           "should have been BC_PUSH_ARG_2");

    vm_oop_t argument = GetFrame()->GetArgument(argIndex, contextLevel);

    GetFrame()->Push(argument);
}

void Interpreter::doPushField(uint8_t fieldIndex) {
    assert(fieldIndex != 0 && fieldIndex != 1 &&
           "should have been BC_PUSH_FIELD_0|1");

    doPushFieldWithIndex(fieldIndex);
}

void Interpreter::doPushFieldWithIndex(uint8_t fieldIndex) {
    vm_oop_t self = GetSelf();
    vm_oop_t o = nullptr;

    if (unlikely(IS_TAGGED(self))) {
        o = nullptr;
        ErrorExit("Integers do not have fields!");
    } else {
        o = ((VMObject*)self)->GetField(fieldIndex);
    }

    GetFrame()->Push(o);
}

void Interpreter::doReturnFieldWithIndex(uint8_t fieldIndex) {
    vm_oop_t self = GetSelf();
    vm_oop_t o = nullptr;

    if (unlikely(IS_TAGGED(self))) {
        o = nullptr;
        ErrorExit("Integers do not have fields!");
    } else {
        o = ((VMObject*)self)->GetField(fieldIndex);
    }

    popFrameAndPushResult(o);
}

void Interpreter::doPushBlock(VMInvokable* blockMethod) {
    uint8_t const numOfArgs = blockMethod->GetNumberOfArguments();

    VMBlock* block = Universe::NewBlock(blockMethod, GetFrame(), numOfArgs);
    if (block != nullptr) {
        GetFrame()->Push(block);
    }
}

// Returns false if the global is unknown, and Start() has to send
// #unknownGlobal:.
bool Interpreter::doPushGlobal(VMSymbol* globalName) {
    vm_oop_t global = Universe::GetGlobal(globalName);

    if (global == nullptr) {
        return false;
    }
    GetFrame()->Push(global);
    return true;
}

#ifdef FRUCTUS
// all globals are in the image, so this is an error
bool Interpreter::SendUnknownGlobal(VMSymbol* globalName) {
    char name[16];
    FormatTagged(name, "sel", (uintptr_t)globalName);
    ErrorPrintf("ERROR: Unknown global %s\n", name);
    GetFrame()->PrintStackTrace();
    Quit(ERR_FAIL);
}
#else
bool Interpreter::SendUnknownGlobal(VMSymbol* globalName) {
    VMSymbol* selector = SymbolFor(unknownGlobal);
    if (selector == nullptr) {
        return false;
    }
    vm_oop_t arguments[] = {globalName};
    vm_oop_t self = GetSelf();

    // it needs 2 slots, and the push of the global had 1, see
    // VMFrame::EXTRA_STACK_SLOTS
    return AS_OBJ(self)->Send(selector, arguments, 1);
}
#endif

void Interpreter::doPop() {
    GetFrame()->Pop();
}

void Interpreter::doPopLocal(uint8_t bc1, uint8_t bc2) {
    vm_oop_t o = GetFrame()->Pop();

    GetFrame()->SetLocal(bc1, bc2, o);
}

void Interpreter::doPopLocalWithIndex(uint8_t localIndex) {
    vm_oop_t o = GetFrame()->Pop();
    GetFrame()->SetLocal(localIndex, o);
}

void Interpreter::doPopArgument(uint8_t bc1, uint8_t bc2) {
    vm_oop_t o = GetFrame()->Pop();
    GetFrame()->SetArgument(bc1, bc2, o);
}

void Interpreter::doPopField(uint8_t fieldIndex) {
    doPopFieldWithIndex(fieldIndex);
}

void Interpreter::doPopFieldWithIndex(uint8_t fieldIndex) {
    vm_oop_t self = GetSelf();
    vm_oop_t o = GetFrame()->Pop();

    if (unlikely(IS_TAGGED(self))) {
        ErrorExit("Integers do not have fields that can be set");
    } else {
        ((VMObject*)self)->SetField(fieldIndex, o);
    }
}

void Interpreter::doSend(uint8_t* site) {
    vm_oop_t receiver = GetFrame()->GetStackElement(
        site[SendSite::numberOfArguments] - 1);
    assert(IsValidObject(receiver));
    VMClass* receiverClass = CLASS_OF(receiver);
    recordStat(ReceiverType, receiverClass);

    if (receiverClass == SendSite::GetReceiverClass(site, BC_SEND)) {
        auto* invokable = (VMInvokable*)SendSite::GetTarget(site, BC_SEND);
        recordStat(CallStats, receiverClass, invokable);
        invokable->Invoke(GetFrame());
    } else {
        sendMiss(site, receiverClass);
    }
}

void Interpreter::doUnarySend(uint8_t* site) {
    vm_oop_t receiver = GetFrame()->GetStackElement(0);
    assert(IsValidObject(receiver));
    VMClass* receiverClass = CLASS_OF(receiver);
    recordStat(ReceiverType, receiverClass);

    if (receiverClass == SendSite::GetReceiverClass(site, BC_SEND_1)) {
        auto* invokable = (VMInvokable*)SendSite::GetTarget(site, BC_SEND_1);
        recordStat(CallStats, receiverClass, invokable);
        invokable->Invoke1(GetFrame());
    } else {
        sendMiss(site, receiverClass);
    }
}

void Interpreter::doSuperSend(uint8_t* site) {
#ifdef FRUCTUS
    // the image writer links every super send, so invokables need no holder
    // to find the superclass
    assert(SendSite::GetReceiverClass(site, BC_SUPER_SEND) != nullptr);
    ((VMInvokable*)SendSite::GetTarget(site, BC_SUPER_SEND))
        ->Invoke(GetFrame());
}
#else
    // a linked super send has its invokable whatever the receiver is
    if (SendSite::GetReceiverClass(site) != nullptr) {
        auto* invokable = (VMInvokable*)SendSite::GetTarget(site);
        invokable->Invoke(GetFrame());
        return;
    }

    auto* signature = (VMSymbol*)SendSite::GetTarget(site);
    VMFrame const* const ctxt = GetFrame()->GetOuterContext();
    VMMethod const* const realMethod = ctxt->GetMethod();
    VMClass const* const holder = realMethod->GetHolder();
    assert(holder->HasSuperClass());
    auto* super = (VMClass*)holder->GetSuperClass();
    auto* invokable = super->LookupInvokable(signature);

    if (invokable != nullptr) {
        SendSite::Link(site, invokable, super);
#ifndef FRUCTUS
        write_barrier(method, invokable);
        write_barrier(method, super);
#endif
        invokable->Invoke(GetFrame());
    } else {
        triggerDoesNotUnderstand(signature);
    }
}
#endif

void Interpreter::doReturnLocal() {
    vm_oop_t result = GetFrame()->Pop();
    popFrameAndPushResult(result);
}

// Answers false, and changes nothing, if the block has escaped and the send
// of #escapedBlock: failed.
bool Interpreter::doReturnNonLocal() {
    vm_oop_t result = GetFrame()->Pop();

    VMFrame const* const context = GetFrame()->GetOuterContext();

    if (!context->HasPreviousFrame()) {
#ifdef FRUCTUS
        // a closed world can't recover, so it's an error, and escapedBlock:
        // is never sent, like doesNotUnderstand:arguments:
        ErrorPrint("ERROR: Block has escaped and cannot be executed\n");
        GetFrame()->PrintStackTrace();
        Quit(ERR_FAIL);
#else
        return sendEscapedBlock(result);
#endif
    }

    while (GetFrame() != context) {
        popFrame();
    }

    popFrameAndPushResult(result);
    return true;
}

#ifndef FRUCTUS
// Sends #escapedBlock: to the sender of the block's #value, in place of that
// send: the block's frame returns, and its receiver and arguments are popped
// from the sender's stack. If the send fails, it puts them back and pushes
// result, so that the return can run again.
bool Interpreter::sendEscapedBlock(vm_oop_t result) {
    VMSymbol* selector = SymbolFor(escapedBlock);
    if (selector == nullptr) {
        GetFrame()->Push(result);
        return false;
    }

    VMFrame* const blockFrame = GetFrame();
    auto* block =
        static_cast<VMBlock*>(blockFrame->GetArgumentInCurrentContext(0));
    VMFrame* const prevFrame = blockFrame->GetPreviousFrame();
    VMFrame const* const outerContext = prevFrame->GetOuterContext();
    vm_oop_t sender = outerContext->GetArgumentInCurrentContext(0);
    vm_oop_t arguments[] = {block};

    // the block's frame returns only if the send succeeds, so it keeps its
    // previous frame until then
    SetFrame(prevFrame);
    uint8_t const numberOfArgs = blockFrame->GetMethod()->GetNumberOfArguments();
    std::vector<vm_oop_t> blockArguments(numberOfArgs);
    for (som_int_t i = numberOfArgs - 1; i >= 0; --i) {
        blockArguments[i] = prevFrame->Pop();
    }

    // it needs 2 slots, and the block had at least 1, see
    // VMFrame::EXTRA_STACK_SLOTS
    if (!AS_OBJ(sender)->Send(selector, arguments, 1)) {
        for (vm_oop_t arg : blockArguments) {
            prevFrame->Push(arg);
        }
        SetFrame(blockFrame);
        blockFrame->Push(result);
        return false;
    }
    blockFrame->ClearPreviousFrame();
    return true;
}
#endif

#ifdef FRUCTUS
/**
 * Sets val to val + delta, for a small integer on its tagged word, where an
 * overflow of som_int_t is a result that isn't a small integer, and otherwise
 * in IntegerWideAdd(), which checks that val is an integer. Answers false if
 * the result couldn't be allocated, which the fast path never does.
 */
static inline bool integerAdd(vm_oop_t& val, som_int_t delta) {
    som_int_t result = 0;
    if (likely(IS_TAGGED(val)) &&
        !__builtin_add_overflow((som_int_t)val, 2 * delta, &result)) {
        val = (vm_oop_t)result;
        return true;
    }
    val = IntegerWideAdd(val, NEW_INT(delta));
    return val != nullptr;
}
#else
/**
 * Sets val to val + delta, for a small integer or a double. Answers false if
 * the result couldn't be allocated.
 */
static inline bool integerAdd(vm_oop_t& val, som_int_t delta) {
    if (IS_SMALL_INT(val)) {
        val = NEW_INT(SMALL_INT_VAL(val) + delta);
    } else if (CLASS_OF(val) == load_ptr(doubleClass)) {
        double const d = static_cast<VMDouble*>(val)->GetEmbeddedDouble();
        val = Universe::NewDouble(d + (double)delta);
    } else {
        ErrorExit("unsupported");
    }
    return val != nullptr;
}
#endif

// The increments answer false, and change nothing, if the result couldn't be
// allocated.
bool Interpreter::doInc() {
    vm_oop_t val = GetFrame()->Top();

    if (!integerAdd(val, 1)) {
        return false;
    }

    if (IS_TAGGED(val)) {
        GetFrame()->SetTopNoBarrier(val);
    } else {
        GetFrame()->SetTop(val);
    }
    return true;
}

bool Interpreter::doDec() {
    vm_oop_t val = GetFrame()->Top();

    if (!integerAdd(val, -1)) {
        return false;
    }

    if (IS_TAGGED(val)) {
        GetFrame()->SetTopNoBarrier(val);
    } else {
        GetFrame()->SetTop(val);
    }
    return true;
}

bool Interpreter::doIncField(uint8_t fieldIndex) {
    vm_oop_t self = GetSelf();

    if (unlikely(IS_TAGGED(self))) {
        ErrorExit("Integers do not have fields!");
    }

    auto* selfObj = (VMObject*)self;

    vm_oop_t val = selfObj->GetField(fieldIndex);

    if (!integerAdd(val, 1)) {
        return false;
    }

    selfObj->SetField(fieldIndex, val);
    return true;
}

bool Interpreter::doIncFieldPush(uint8_t fieldIndex) {
    vm_oop_t self = GetSelf();

    if (unlikely(IS_TAGGED(self))) {
        ErrorExit("Integers do not have fields!");
    }

    auto* selfObj = (VMObject*)self;

    vm_oop_t val = selfObj->GetField(fieldIndex);

    if (!integerAdd(val, 1)) {
        return false;
    }

    selfObj->SetField(fieldIndex, val);
    if (IS_TAGGED(val)) {
        GetFrame()->PushNoBarrier(val);
    } else {
        GetFrame()->Push(val);
    }
    return true;
}

bool Interpreter::checkIsGreater() {
    vm_oop_t top = GetFrame()->Top();
    vm_oop_t top2 = GetFrame()->Top2();

    if (IS_SMALL_INT(top) && IS_SMALL_INT(top2)) {
        return SMALL_INT_VAL(top) > SMALL_INT_VAL(top2);
    }
#ifdef FRUCTUS
    if (IsInteger(top) && IsInteger(top2)) {
        return IntegerWideGreaterThan(top, top2) == load_ptr(trueObject);
    }
#endif
    if (IS_DOUBLE(top) && IS_DOUBLE(top2)) {
        return AS_DOUBLE(top) > AS_DOUBLE(top2);
    }

    return false;
}

void Interpreter::WalkGlobals(walk_heap_fn walk) {
    method = load_ptr(static_cast<GCMethod*>(walk(tmp_ptr(method))));

    // Get the current frame and mark it.
    // Since marking is done recursively, this automatically
    // marks the whole stack
    frame = load_ptr(static_cast<GCFrame*>(walk(tmp_ptr(frame))));
}

void Interpreter::startGC() {
    GetHeap<HEAP_CLS>()->SafePoint();
    method = GetFrame()->GetMethod();
}

VMMethod* Interpreter::GetMethod() {
    return GetFrame()->GetMethod();
}

uint8_t* Interpreter::GetBytecodes() {
    return method->GetBytecodes();
}
