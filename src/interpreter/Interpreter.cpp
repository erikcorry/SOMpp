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
#if USE_FRAME_STACK
  #include "../memory/SlidingHeap.h"
#endif
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

// Start() works on the stack of the current frame through its local sp, see
// SAVE_PC(). The stores into the current frame need no write barrier, since
// the frame is remembered when it stops being the current one, and before a
// collection, see SetFrame().
static inline void push(gc_oop_t*& sp, vm_oop_t val) {
    assert(GetInterpreter()->FitsInFrame(sp + 1));
    *++sp = store_with_separate_barrier(val);
}

static inline void setTop(gc_oop_t* sp, vm_oop_t val) {
    *sp = store_with_separate_barrier(val);
}

static inline vm_oop_t stackElement(gc_oop_t* sp, size_t index) {
    return load_ptr(sp[-std::make_signed_t<som_int_t>(index)]);
}

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
    gc_oop_t* sp = nullptr;
    RELOAD_PC();

    void const* const loopTargets[] = {&&LABEL_BC_HALT,
                                       &&LABEL_BC_DUP,
                                       &&LABEL_BC_DUP_SECOND,
                                       &&LABEL_BC_PUSH_LOCAL,
                                       &&LABEL_NOT_STACK_RELATIVE,
                                       &&LABEL_NOT_STACK_RELATIVE,
                                       &&LABEL_NOT_STACK_RELATIVE,
                                       &&LABEL_BC_PUSH_ARGUMENT,
                                       &&LABEL_NOT_STACK_RELATIVE,
                                       &&LABEL_NOT_STACK_RELATIVE,
                                       &&LABEL_NOT_STACK_RELATIVE,
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
                                       &&LABEL_NOT_STACK_RELATIVE,
                                       &&LABEL_NOT_STACK_RELATIVE,
                                       &&LABEL_NOT_STACK_RELATIVE,
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
                                       &&LABEL_BC_JUMP2_BACKWARD,
                                       &&LABEL_BC_SEND_EQEQ,
                                       &&LABEL_BC_STATIC_SEND,
                                       &&LABEL_BC_STATIC_SEND_1,
                                       &&LABEL_BC_NOT_UNDERSTOOD,
                                       &&LABEL_BC_PUSH_STACK,
                                       &&LABEL_BC_POP_STACK};

    goto* loopTargets[*pc];

    //
    // THIS IS THE former interpretation loop
LABEL_BC_HALT:
    PROLOGUE();
    SAVE_NEXT_PC(1);
    return load_ptr(*sp);  // handle the halt bytecode

LABEL_BC_DUP:
    PROLOGUE();
    push(sp, load_ptr(*sp));
    NEXT_NOGC(1);

LABEL_BC_DUP_SECOND:
    PROLOGUE();
    push(sp, stackElement(sp, 1));
    NEXT_NOGC(1);

    // the variables of an outer context, see MakeStackRelative()
LABEL_BC_PUSH_LOCAL:
    PROLOGUE();
    assert(pc[2] != 0 && "should have been BC_PUSH_STACK");
    push(sp, frame->GetLocal(pc[1], pc[2]));
    NEXT_NOGC(3);

LABEL_BC_PUSH_ARGUMENT:
    PROLOGUE();
    assert(pc[2] != 0 && "should have been BC_PUSH_STACK");
    push(sp, frame->GetArgument(pc[1], pc[2]));
    NEXT_NOGC(3);

    // The compiler makes the arguments and locals of the current context
    // PUSH_STACK and POP_STACK, see VMMethod::MakeStackRelative().
LABEL_NOT_STACK_RELATIVE:
    assert(false && "MakeStackRelative() replaces this bytecode");
    __builtin_unreachable();

LABEL_BC_PUSH_STACK:
    PROLOGUE();
    push(sp, stackElement(sp, pc[1]));
    NEXT_NOGC(2);

LABEL_BC_POP_STACK:
    PROLOGUE();
    setTop(sp - pc[1], load_ptr(*sp));
    sp--;
    NEXT_NOGC(2);

LABEL_BC_PUSH_FIELD:
    PROLOGUE();
    assert(pc[1] != 0 && pc[1] != 1 && "should have been BC_PUSH_FIELD_0|1");
    push(sp, getField(pc[1]));
    NEXT_NOGC(2);

LABEL_BC_PUSH_FIELD_0:
    PROLOGUE();
    push(sp, getField(0));
    NEXT_NOGC(1);

LABEL_BC_PUSH_FIELD_1:
    PROLOGUE();
    push(sp, getField(1));
    NEXT_NOGC(1);

LABEL_BC_PUSH_BLOCK:
    PROLOGUE();
    {
        auto* blockMethod = (VMInvokable*)LoadWord(pc + 1);
        VMBlock* block = Universe::NewBlock(
            blockMethod, frame, blockMethod->GetNumberOfArguments());
        if (block != nullptr) {
            push(sp, block);
        }
    }
    NEXT_GC(1 + sizeof(void*));

LABEL_BC_PUSH_CONSTANT:
    PROLOGUE();
    push(sp, (vm_oop_t)LoadWord(pc + 1));
    NEXT_NOGC(1 + sizeof(void*));

LABEL_BC_PUSH_0:
    PROLOGUE();
    push(sp, NEW_INT(0));
    NEXT_NOGC(1);

LABEL_BC_PUSH_1:
    PROLOGUE();
    push(sp, NEW_INT(1));
    NEXT_NOGC(1);

LABEL_BC_PUSH_SMALL_INT:
    PROLOGUE();
    push(sp, NEW_INT((int8_t)pc[1]));
    NEXT_NOGC(2);

LABEL_BC_PUSH_NIL:
    PROLOGUE();
    push(sp, load_ptr(nilObject));
    NEXT_NOGC(1);

LABEL_BC_PUSH_GLOBAL:
    PROLOGUE();
    {
        auto* globalName = (VMSymbol*)LoadWord(pc + 1);
        pc += 1 + sizeof(void*);
        vm_oop_t global = Universe::GetGlobal(globalName);
        if (global != nullptr) {
            push(sp, global);
        } else {
            // sends #unknownGlobal:, which pushes a new frame
            SAVE_PC();
            SendUnknownGlobal(globalName);
            RELOAD_PC();
        }
    }
    DISPATCH_GC(1 + sizeof(void*));

LABEL_BC_POP:
    PROLOGUE();
    sp--;
    NEXT_NOGC(1);

LABEL_BC_POP_LOCAL:
    PROLOGUE();
    frame->SetLocal(pc[1], pc[2], load_ptr(*sp--));
    NEXT_NOGC(3);

LABEL_BC_POP_ARGUMENT:
    PROLOGUE();
    frame->SetArgument(pc[1], pc[2], load_ptr(*sp--));
    NEXT_NOGC(3);

LABEL_BC_POP_FIELD:
    PROLOGUE();
    setField(pc[1], load_ptr(*sp--));
    NEXT_NOGC(2);

LABEL_BC_POP_FIELD_0:
    PROLOGUE();
    setField(0, load_ptr(*sp--));
    NEXT_NOGC(1);

LABEL_BC_POP_FIELD_1:
    PROLOGUE();
    setField(1, load_ptr(*sp--));
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
#ifdef FRUCTUS
    // the image writer makes every super send a STATIC_SEND
    __builtin_unreachable();
#else
    PROLOGUE();
    SAVE_NEXT_PC(SendSite::Length(BC_SUPER_SEND));
    doSuperSend(pc);
    RELOAD_PC();
    DISPATCH_GC(SendSite::Length(BC_SUPER_SEND));
#endif

    // A send that can only call one invokable, see SendSite.h.
LABEL_BC_STATIC_SEND:
    PROLOGUE();
    SAVE_NEXT_PC(SendSite::staticLength);
    ((VMInvokable*)LoadWord(pc + SendSite::staticTarget))->Invoke(GetFrame());
    RELOAD_PC();
    DISPATCH_GC(SendSite::staticLength);

LABEL_BC_STATIC_SEND_1:
    PROLOGUE();
    SAVE_NEXT_PC(SendSite::staticLength);
    ((VMInvokable*)LoadWord(pc + SendSite::staticTarget))->Invoke1(GetFrame());
    RELOAD_PC();
    DISPATCH_GC(SendSite::staticLength);

    // A send of a selector no class has, see SendSite.h.
LABEL_BC_NOT_UNDERSTOOD:
#ifdef FRUCTUS
    PROLOGUE();
    SAVE_NEXT_PC(SendSite::notUnderstoodLength);
    triggerDoesNotUnderstand(
        (VMSymbol*)LoadWord(pc + SendSite::notUnderstoodSelector),
        CLASS_OF(stackElement(sp, pc[SendSite::numberOfArguments] - 1)));
#else
    // only the fructus image writer makes them
    __builtin_unreachable();
#endif

LABEL_BC_RETURN_LOCAL:
    PROLOGUE();
    // the returning frame's stack_ptr doesn't matter any more
    popFrameAndPushResult(load_ptr(*sp));
    RELOAD_PC();
    DISPATCH_NOGC();

LABEL_BC_RETURN_NON_LOCAL:
    PROLOGUE();
    SAVE_PC();
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
    if (!incTop(sp, 1)) {
        goto RETRY;
    }
    if (USE_TAGGING) {
        NEXT_NOGC(1);
    } else {
        // without integer tagging incTop() allocates memory and the IfNil
        // benchmark will allocate, but not reach a GC point, and run out of
        // memory
        NEXT_GC(1);
    }

LABEL_BC_DEC:
    PROLOGUE();
    if (!incTop(sp, -1)) {
        goto RETRY;
    }
    NEXT_NOGC(1);

LABEL_BC_INC_FIELD:
    PROLOGUE();
    if (incField(pc[1]) == nullptr) {
        goto RETRY;
    }
    NEXT_NOGC(2);

LABEL_BC_INC_FIELD_PUSH:
    PROLOGUE();
    {
        vm_oop_t const val = incField(pc[1]);
        if (val == nullptr) {
            goto RETRY;
        }
        push(sp, val);
    }
    NEXT_NOGC(2);

LABEL_BC_JUMP: {
    uint8_t const offset = pc[1];
    pc += offset;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_FALSE_POP: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(falseObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
    } else {
        pc += 3;
    }
    sp--;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_TRUE_POP: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(trueObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
    } else {
        pc += 3;
    }
    sp--;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_FALSE_TOP_NIL: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(falseObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
        setTop(sp, load_ptr(nilObject));
    } else {
        sp--;
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_TRUE_TOP_NIL: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(trueObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
        setTop(sp, load_ptr(nilObject));
    } else {
        sp--;
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_NOT_NIL_POP: {
    vm_oop_t val = load_ptr(*sp);
    if (val != load_ptr(nilObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
    } else {
        pc += 3;
    }
    sp--;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_NIL_POP: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(nilObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
    } else {
        pc += 3;
    }
    sp--;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_NOT_NIL_TOP_TOP: {
    vm_oop_t val = load_ptr(*sp);
    if (val != load_ptr(nilObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
        // GetFrame()->SetTop(val);
    } else {
        sp--;
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_ON_NIL_TOP_TOP: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(nilObject)) {
        uint8_t const offset = pc[1];
        pc += offset;
        // GetFrame()->SetTop(val);
    } else {
        sp--;
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP_IF_GREATER: {
    if (checkIsGreater(load_ptr(sp[0]), load_ptr(sp[-1]))) {
        pc += pc[1];
        sp -= 2;
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
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(falseObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
    } else {
        pc += 3;
    }
    sp--;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_TRUE_POP: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(trueObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
    } else {
        pc += 3;
    }
    sp--;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_FALSE_TOP_NIL: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(falseObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
        setTop(sp, load_ptr(nilObject));
    } else {
        sp--;
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_TRUE_TOP_NIL: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(trueObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
        setTop(sp, load_ptr(nilObject));
    } else {
        sp--;
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_NOT_NIL_POP: {
    vm_oop_t val = load_ptr(*sp);
    if (val != load_ptr(nilObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
    } else {
        pc += 3;
    }
    sp--;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_NIL_POP: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(nilObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
    } else {
        pc += 3;
    }
    sp--;
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_NOT_NIL_TOP_TOP: {
    vm_oop_t val = load_ptr(*sp);
    if (val != load_ptr(nilObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
        // GetFrame()->SetTop(val);
    } else {
        sp--;
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_ON_NIL_TOP_TOP: {
    vm_oop_t val = load_ptr(*sp);
    if (val == load_ptr(nilObject)) {
        uint16_t const offset =
            ComputeOffset(pc[1],
                          pc[2]);
        pc += offset;
        // GetFrame()->SetTop(val);
    } else {
        sp--;
        pc += 3;
    }
}
    DISPATCH_NOGC();

LABEL_BC_JUMP2_IF_GREATER: {
    if (checkIsGreater(load_ptr(sp[0]), load_ptr(sp[-1]))) {
        pc +=
            ComputeOffset(pc[1],
                          pc[2]);
        sp -= 2;
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

    // A send of #==, answered here by identity for nil, a small integer and a
    // symbol. The core library doesn't override #== for nil or symbols, and an
    // integer is a small one whenever it fits, so a small integer is only
    // equal to itself: Integer>>== is a primitive that says so, even for a
    // double of the same value. Anything else is a send.
LABEL_BC_SEND_EQEQ: {
    PROLOGUE();
    vm_oop_t const arg = load_ptr(sp[0]);
    vm_oop_t const receiver = load_ptr(sp[-1]);
    if (receiver == load_ptr(nilObject) || IS_TAGGED(receiver) ||
        CLASS_OF(receiver) == load_ptr(symbolClass)) {
        sp--;
        setTop(sp, load_ptr(receiver == arg ? trueObject : falseObject));
        NEXT_NOGC(SendSite::Length(BC_SEND_EQEQ));
    }
}
    SAVE_NEXT_PC(SendSite::Length(BC_SEND_EQEQ));
    doSend(pc);
    RELOAD_PC();
    DISPATCH_GC(SendSite::Length(BC_SEND_EQEQ));

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
//
// Start() stores into the current frame without the write barrier, so the
// frame is remembered, as if it pointed into young space, when it stops being
// the current frame, here, and before a collection, in startGC(). A frame
// that returns is remembered too, since a block may still have it as its
// context. Stores into other frames, like a block's into its outer contexts,
// still have the barrier.
//
// A frame on the frame stack isn't remembered, since the stack is a root at
// every collection, see WalkGlobals(). When a frame on the stack stops being
// the current frame for one in the heap, stackTop says where the next frame
// on the stack goes: above the stack frame's stack if it called the heap
// frame, and where the stack frame was if it returned, or otherwise left.
void Interpreter::SetFrame(VMFrame* frm) {
    if (frame != nullptr) {
#if USE_FRAME_STACK
        if (!OnFrameStack(frame)) {
            remember_object(frame);
        } else if (!OnFrameStack(frm)) {
            stackTop = frm->GetPreviousFrame() == frame ? frame->stack_ptr + 1
                                                        : (gc_oop_t*)frame;
        }
#else
        remember_object(frame);
#endif
    }
    frame = frm;
    method = frm->GetMethod();
}

void Interpreter::ClearFrames() {
    frame = nullptr;
    method = nullptr;
#if USE_FRAME_STACK
    SlidingHeap const* const heap = GetHeap<SlidingHeap>();
    stackTop = (gc_oop_t*)heap->GetStackBase();
    stackEnd = heap->GetStackEnd();
  #ifndef FRUCTUS
    stackBase = heap->GetStackBase();
  #endif
#endif
}

#if USE_FRAME_STACK
bool Interpreter::growFrameStack(char* end) {
    SlidingHeap* const heap = GetHeap<SlidingHeap>();
    if (!heap->GrowStack(end)) {
        return false;
    }
    stackEnd = heap->GetStackEnd();
    return true;
}
#endif

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

    // the frame is the current one again, which needs no write barrier, see
    // SetFrame()
    GetFrame()->stack_ptr -= numberOfArgs;
    *++GetFrame()->stack_ptr = store_with_separate_barrier(result);
}

#ifdef FRUCTUS
// The slow path of a send, where the receiver's classId isn't in the first
// range of the target: look in its other ranges, and then in the others in
// its ring, and link the send to the one that has it, see SendSite.h.
void Interpreter::sendMiss(uint8_t* site, VMClass* receiverClass) {
    auto* const target = (VMInvokable*)SendSite::GetTarget(site);
    size_t const classId = receiverClass->GetClassId();
    VMInvokable* invokable = target;
    if (!target->IsMethodOf(classId, 1)) {
        do {
            invokable = invokable->GetNextWithSignature();
            if (invokable == target) {
                // the ring's selector, see tools/fructus-trace
                char selector[16];
                FormatTagged(selector, "method", (uintptr_t)target);
                DoesNotUnderstand(selector, receiverClass);
            }
        } while (!invokable->IsMethodOf(classId));
        // On fructus the invokables are all in the image, which the
        // collector doesn't move or free, and the methods are above 32K,
        // which the cards don't cover.
        SendSite::Link(site, invokable);
    }
    recordStat(CallStats, receiverClass, invokable);
    if (site[0] == BC_SEND_1) {
        invokable->Invoke1(GetFrame());
    } else {
        invokable->Invoke(GetFrame());
    }
}
#else
// The slow path of a send, which looks the selector up, and links the send
// if it's unlinked, see SendSite.h.
void Interpreter::sendMiss(uint8_t* site, VMClass* receiverClass) {
    bool const isUnlinked = SendSite::GetReceiverClass(site) == nullptr;
    VMSymbol* selector = method->GetSendSelector(site - method->GetBytecodes());
    VMInvokable* invokable = receiverClass->LookupInvokable(selector);

    if (invokable == nullptr) {
        triggerDoesNotUnderstand(selector);
        return;
    }
    if (isUnlinked) {
        SendSite::Link(site, invokable, receiverClass);
        write_barrier(method, invokable);
        write_barrier(method, receiverClass);
    }
    recordStat(CallStats, receiverClass, invokable);
    if (site[0] == BC_SEND_1) {
        invokable->Invoke1(GetFrame());
    } else {
        invokable->Invoke(GetFrame());
    }
}
#endif

#ifdef FRUCTUS
// a closed world: the program can't have the selector, so it's an error, and
// doesNotUnderstand:arguments: is never sent. A send of a selector that no
// class has is NOT_UNDERSTOOD, which has the selector, see SendSite.h.
// Selectors and classes have no names, so this is in the form
// tools/fructus-trace translates.
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
        GetFrame()->PushIntoCurrent(receiver);
        for (size_t i = 0; i < (size_t)numberOfArgs - 1; ++i) {
            GetFrame()->PushIntoCurrent(argumentsArray->GetIndexableField(i));
        }
    }
}
#endif

vm_oop_t Interpreter::getField(uint8_t fieldIndex) {
    vm_oop_t self = GetSelf();

    if (unlikely(IS_TAGGED(self))) {
        ErrorExit("Integers do not have fields!");
    }
    return ((VMObject*)self)->GetField(fieldIndex);
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

void Interpreter::setField(uint8_t fieldIndex, vm_oop_t o) {
    vm_oop_t self = GetSelf();

    if (unlikely(IS_TAGGED(self))) {
        ErrorExit("Integers do not have fields that can be set");
    } else {
        ((VMObject*)self)->SetField(fieldIndex, o);
    }
}

// Whether the send at `site`, whose bytecode is `bc`, calls its target for
// an instance of `receiverClass` without a lookup, see SendSite.h.
static inline bool hits(const uint8_t* site, uint8_t bc,
                        VMClass* receiverClass) {
#ifdef FRUCTUS
    return ((VMInvokable*)SendSite::GetTarget(site, bc))
        ->IsFirstMethodOf(receiverClass->GetClassId());
#else
    return receiverClass == SendSite::GetReceiverClass(site, bc);
#endif
}

void Interpreter::doSend(uint8_t* site) {
    vm_oop_t receiver = GetFrame()->GetStackElement(
        site[SendSite::numberOfArguments] - 1);
    assert(IsValidObject(receiver));
    VMClass* receiverClass = CLASS_OF(receiver);
    recordStat(ReceiverType, receiverClass);

    if (hits(site, BC_SEND, receiverClass)) {
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

    if (hits(site, BC_SEND_1, receiverClass)) {
        auto* invokable = (VMInvokable*)SendSite::GetTarget(site, BC_SEND_1);
        recordStat(CallStats, receiverClass, invokable);
        invokable->Invoke1(GetFrame());
    } else {
        sendMiss(site, receiverClass);
    }
}

#ifndef FRUCTUS
// On fructus the image writer makes every super send a STATIC_SEND, so
// invokables need no holder to find the superclass
void Interpreter::doSuperSend(uint8_t* site) {
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
        write_barrier(method, invokable);
        write_barrier(method, super);
        invokable->Invoke(GetFrame());
    } else {
        triggerDoesNotUnderstand(signature);
    }
}
#endif

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
        GetFrame()->PushIntoCurrent(result);
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
            prevFrame->PushIntoCurrent(arg);
        }
        SetFrame(blockFrame);
        blockFrame->PushIntoCurrent(result);
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
 * Sets val to val + delta, for an integer or a double. Answers false if the
 * result couldn't be allocated.
 */
static inline bool integerAdd(vm_oop_t& val, som_int_t delta) {
    if (IS_SMALL_INT(val)) {
        val = NEW_INT(SMALL_INT_VAL(val) + delta);
    } else if (IS_BIG_INT(val)) {
        val = AS_BIG_INT(val)->Add(delta);
    } else if (CLASS_OF(val) == load_ptr(doubleClass)) {
        double const d = static_cast<VMDouble*>(val)->GetEmbeddedDouble();
        val = Universe::NewDouble(d + (double)delta);
    } else {
        ErrorExit("unsupported");
    }
    return val != nullptr;
}
#endif

// The increments answer false, or nullptr, and change nothing, if the result
// couldn't be allocated.
bool Interpreter::incTop(gc_oop_t* sp, som_int_t delta) {
    vm_oop_t val = load_ptr(*sp);

    if (!integerAdd(val, delta)) {
        return false;
    }

    // the top is in the current frame, which needs no write barrier, see
    // SetFrame()
    *sp = store_with_separate_barrier(val);
    return true;
}

vm_oop_t Interpreter::incField(uint8_t fieldIndex) {
    vm_oop_t self = GetSelf();

    if (unlikely(IS_TAGGED(self))) {
        ErrorExit("Integers do not have fields!");
    }

    auto* selfObj = (VMObject*)self;

    vm_oop_t val = selfObj->GetField(fieldIndex);

    if (!integerAdd(val, 1)) {
        return nullptr;
    }

    selfObj->SetField(fieldIndex, val);
    return val;
}

bool Interpreter::checkIsGreater(vm_oop_t top, vm_oop_t top2) {
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

#if USE_FRAME_STACK
    // The live part of the frame stack is a root, word by word, from the
    // most recent frame down. Its frames follow each other with no gaps, and
    // the words of their headers that aren't oops are nullptr, or point
    // outside what's collected, see ideas/frame-stack.md.
    auto* const bottom = (gc_oop_t*)GetHeap<SlidingHeap>()->GetStackBase();
    for (gc_oop_t* p = FrameStackTop(); p > bottom;) {
        p--;
        if (*p != nullptr) {
            *p = walk(*p);
        }
    }
#endif

    // Get the current frame and mark it.
    // Since marking is done recursively, this automatically
    // marks the whole stack
    frame = load_ptr(static_cast<GCFrame*>(walk(tmp_ptr(frame))));
}

bool VMFrame::IsCurrent() const {
    return this == GetInterpreter()->GetFrame();
}

void Interpreter::startGC() {
    // see SetFrame()
    remember_object(GetFrame());
    GetHeap<HEAP_CLS>()->SafePoint();
    method = GetFrame()->GetMethod();
}

VMMethod* Interpreter::GetMethod() {
    return GetFrame()->GetMethod();
}

uint8_t* Interpreter::GetBytecodes() {
    return method->GetBytecodes();
}
