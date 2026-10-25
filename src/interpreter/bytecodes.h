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
#include <string.h>

#ifndef FRUCTUS
  #include <vector>
#endif

#include "../misc/defs.h"

// bytecode constants used by SOM++

// clang-format off
#define BC_HALT                    0
#define BC_DUP                     1
#define BC_DUP_SECOND              2
#define BC_PUSH_LOCAL              3
#define BC_PUSH_LOCAL_0            4
#define BC_PUSH_LOCAL_1            5
#define BC_PUSH_LOCAL_2            6
#define BC_PUSH_ARGUMENT           7
#define BC_PUSH_SELF               8
#define BC_PUSH_ARG_1              9
#define BC_PUSH_ARG_2              10
#define BC_PUSH_FIELD              11
#define BC_PUSH_FIELD_0            12
#define BC_PUSH_FIELD_1            13
#define BC_PUSH_BLOCK              14
#define BC_PUSH_CONSTANT           15
#define BC_PUSH_0                  16
#define BC_PUSH_1                  17
#define BC_PUSH_SMALL_INT          18
#define BC_PUSH_NIL                19
#define BC_PUSH_GLOBAL             20
#define BC_POP                     21
#define BC_POP_LOCAL               22
#define BC_POP_LOCAL_0             23
#define BC_POP_LOCAL_1             24
#define BC_POP_LOCAL_2             25
#define BC_POP_ARGUMENT            26
#define BC_POP_FIELD               27
#define BC_POP_FIELD_0             28
#define BC_POP_FIELD_1             29
#define BC_SEND                    30
#define BC_SEND_1                  31
#define BC_SUPER_SEND              32
#define BC_RETURN_LOCAL            33
#define BC_RETURN_NON_LOCAL        34
#define BC_RETURN_SELF             35
#define BC_RETURN_FIELD_0          36
#define BC_RETURN_FIELD_1          37
#define BC_RETURN_FIELD_2          38
#define BC_INC                     39
#define BC_DEC                     40
#define BC_INC_FIELD               41
#define BC_INC_FIELD_PUSH          42
#define BC_JUMP                    43
#define BC_JUMP_ON_FALSE_POP       44
#define BC_JUMP_ON_TRUE_POP        45
#define BC_JUMP_ON_FALSE_TOP_NIL   46
#define BC_JUMP_ON_TRUE_TOP_NIL    47
#define BC_JUMP_ON_NOT_NIL_POP     48
#define BC_JUMP_ON_NIL_POP         49
#define BC_JUMP_ON_NOT_NIL_TOP_TOP 50
#define BC_JUMP_ON_NIL_TOP_TOP     51
#define BC_JUMP_IF_GREATER         52
#define BC_JUMP_BACKWARD           53
#define BC_JUMP2                   54
#define BC_JUMP2_ON_FALSE_POP      55
#define BC_JUMP2_ON_TRUE_POP       56
#define BC_JUMP2_ON_FALSE_TOP_NIL  57
#define BC_JUMP2_ON_TRUE_TOP_NIL   58
#define BC_JUMP2_ON_NOT_NIL_POP    59
#define BC_JUMP2_ON_NIL_POP        60
#define BC_JUMP2_ON_NOT_NIL_TOP_TOP 61
#define BC_JUMP2_ON_NIL_TOP_TOP    62
#define BC_JUMP2_IF_GREATER        63
#define BC_JUMP2_BACKWARD          64
#define BC_SEND_EQEQ               65
#define BC_STATIC_SEND             66
#define BC_STATIC_SEND_1           67
#define BC_NOT_UNDERSTOOD          68
#define BC_PUSH_STACK              69
#define BC_POP_STACK               70

// The sends and returns of a method whose frames are on the frame stack, see
// StackFrameBytecode()
#define BC_SEND_STACK              71
#define BC_SEND_1_STACK            72
#define BC_SEND_EQEQ_STACK         73
#define BC_STATIC_SEND_STACK       74
#define BC_STATIC_SEND_1_STACK     75
#define BC_RETURN_LOCAL_STACK      76
#define BC_RETURN_SELF_STACK       77
#define BC_RETURN_FIELD_0_STACK    78
#define BC_RETURN_FIELD_1_STACK    79
#define BC_RETURN_FIELD_2_STACK    80

#define _LAST_BYTECODE BC_RETURN_FIELD_2_STACK

#define BC_INVALID           255
// clang-format on

// TODO(smarr): port support for these bytecodes
//       they were already named in ported code, and it seemed nicer to just
//       already include that code
// clang-format off
#define BC_SEND_N            250
#define BC_SEND_3            249
#define BC_SEND_2            248
// clang-format on

// properties of the bytecodes

#define FIRST_DOUBLE_BYTE_JUMP_BYTECODE BC_JUMP2
#define NUM_SINGLE_BYTE_JUMP_BYTECODES ((BC_JUMP_BACKWARD - BC_JUMP) + 1)

class Bytecode {
public:
    static char* GetBytecodeName(uint8_t bc) {
        return (char*)bytecodeNames[bc];
    }

    inline static uint8_t GetBytecodeLength(uint8_t bc) {
        return bytecodeLengths[bc];  // Return the length of the given bytecode
    }

    static bool BytecodeDefinitionsAreConsistent();

private:
    static const uint8_t bytecodeLengths[];

    static const char* bytecodeNames[];
};

inline uint16_t ComputeOffset(uint8_t byte1, uint8_t byte2) {
    return ((uint16_t)byte1) | (uint16_t)(((uint16_t)byte2) << 8U);
}

/**
 * Some operands are words, pointers to objects: the constant of
 * PUSH_CONSTANT, the block method of PUSH_BLOCK, the name of PUSH_GLOBAL, and
 * the inline cache of a send, see SendSite.h. They come after the byte
 * operands at any alignment, so they're read and written with memcpy, which
 * is an unaligned load or store. On the host the GC walks them, see VMMethod.
 * On fructus they are never moved or freed, so it doesn't.
 */
uint8_t GetNumberOfWords(uint8_t bc);

inline void* LoadWord(const uint8_t* p) {
    void* value = nullptr;
    memcpy((void*)&value, p, sizeof(value));
    return value;
}

inline void StoreWord(uint8_t* p, const void* value) {
    memcpy(p, (const void*)&value, sizeof(value));
}

/** The operand of a bytecode that has only a word, like PUSH_CONSTANT. */
inline void* GetWordOperand(const uint8_t* bytecode) {
    return LoadWord(bytecode + 1);
}

/** A send, which carries an inline cache, see SendSite.h. */
bool IsSendBytecode(uint8_t bc);
bool IsJumpBytecode(uint8_t bc);
uint8_t IsPushConstBytecode(uint8_t bc);
uint8_t IsPushFieldBytecode(uint8_t bc);
uint8_t IsPushArgBytecode(uint8_t bc);
uint8_t IsPopFieldBytecode(uint8_t bc);
uint8_t IsPopSmthBytecode(uint8_t bc);
uint8_t IsReturnFieldBytecode(uint8_t bc);

#ifndef FRUCTUS
/**
 * The depth of the stack before the bytecode at each index of `bytecodes`,
 * or -1 if no bytecode starts there or it can't be reached. The compiler
 * makes every bytecode run at one depth, however it's reached. The bytecodes
 * must not have static sends, which lose the number of arguments.
 */
std::vector<int> ComputeStackDepths(const uint8_t* bytecodes, size_t length);

/**
 * The bytecode in place of `bc` in a method whose frames are on the frame
 * stack, so that the interpreter knows where the frames are, see
 * Interpreter::enterMethod(), and HeapFrameBytecode() goes back.
 */
uint8_t StackFrameBytecode(uint8_t bc);
uint8_t HeapFrameBytecode(uint8_t bc);
#endif
