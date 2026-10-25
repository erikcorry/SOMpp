#include "bytecodes.h"

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
#include <cstdint>

#include "SendSite.h"

#ifndef FRUCTUS
  #include "../vm/Print.h"
#endif

const uint8_t Bytecode::bytecodeLengths[] = {
    1,                                // BC_HALT
    1,                                // BC_DUP
    1,                                // BC_DUP_SECOND
    3,                                // BC_PUSH_LOCAL
    1,                                // BC_PUSH_LOCAL_0
    1,                                // BC_PUSH_LOCAL_1
    1,                                // BC_PUSH_LOCAL_2
    3,                                // BC_PUSH_ARGUMENT
    1,                                // BC_PUSH_SELF
    1,                                // BC_PUSH_ARG_1
    1,                                // BC_PUSH_ARG_2
    2,                                // BC_PUSH_FIELD
    1,                                // BC_PUSH_FIELD_0
    1,                                // BC_PUSH_FIELD_1
    1 + sizeof(void*),                // BC_PUSH_BLOCK
    1 + sizeof(void*),                // BC_PUSH_CONSTANT
    1,                                // BC_PUSH_0
    1,                                // BC_PUSH_1
    2,                                // BC_PUSH_SMALL_INT
    1,                                // BC_PUSH_NIL
    1 + sizeof(void*),                // BC_PUSH_GLOBAL
    1,                                // BC_POP
    3,                                // BC_POP_LOCAL
    1,                                // BC_POP_LOCAL_0
    1,                                // BC_POP_LOCAL_1
    1,                                // BC_POP_LOCAL_2
    3,                                // BC_POP_ARGUMENT
    2,                                // BC_POP_FIELD
    1,                                // BC_POP_FIELD_0
    1,                                // BC_POP_FIELD_1
    SendSite::Length(BC_SEND),        // BC_SEND
    SendSite::Length(BC_SEND_1),      // BC_SEND_1
    SendSite::Length(BC_SUPER_SEND),  // BC_SUPER_SEND
    1,                                // BC_RETURN_LOCAL
    1,                                // BC_RETURN_NON_LOCAL
    1,                                // BC_RETURN_SELF
    1,                                // BC_RETURN_FIELD_0
    1,                                // BC_RETURN_FIELD_1
    1,                                // BC_RETURN_FIELD_2
    1,                                // BC_INC
    1,                                // BC_DEC
    2,                                // BC_INC_FIELD
    2,                                // BC_INC_FIELD_PUSH

    3,  // BC_JUMP
    3,  // BC_JUMP_ON_FALSE_POP
    3,  // BC_JUMP_ON_TRUE_POP
    3,  // BC_JUMP_ON_FALSE_TOP_NIL
    3,  // BC_JUMP_ON_TRUE_TOP_NIL
    3,  // BC_JUMP_ON_NOT_NIL_POP
    3,  // BC_JUMP_ON_NIL_POP
    3,  // BC_JUMP_ON_NOT_NIL_TOP_TOP
    3,  // BC_JUMP_ON_NIL_TOP_TOP
    3,  // BC_JUMP_IF_GREATER
    3,  // BC_JUMP_BACKWARD

    3,  // BC_JUMP2
    3,  // BC_JUMP2_ON_FALSE_POP
    3,  // BC_JUMP2_ON_TRUE_POP
    3,  // BC_JUMP2_ON_FALSE_TOP_NIL
    3,  // BC_JUMP2_ON_TRUE_TOP_NIL
    3,  // BC_JUMP2_ON_NOT_NIL_POP
    3,  // BC_JUMP2_ON_NIL_POP
    3,  // BC_JUMP2_ON_NOT_NIL_TOP_TOP
    3,  // BC_JUMP2_ON_NIL_TOP_TOP
    3,  // BC_JUMP2_IF_GREATER
    3,  // BC_JUMP2_BACKWARD

    SendSite::Length(BC_SEND_EQEQ),  // BC_SEND_EQEQ
    SendSite::staticLength,          // BC_STATIC_SEND
    SendSite::staticLength,          // BC_STATIC_SEND_1
    SendSite::notUnderstoodLength,   // BC_NOT_UNDERSTOOD
    2,                               // BC_PUSH_STACK
    2,                               // BC_POP_STACK

    SendSite::Length(BC_SEND),       // BC_SEND_STACK
    SendSite::Length(BC_SEND_1),     // BC_SEND_1_STACK
    SendSite::Length(BC_SEND_EQEQ),  // BC_SEND_EQEQ_STACK
    SendSite::staticLength,          // BC_STATIC_SEND_STACK
    SendSite::staticLength,          // BC_STATIC_SEND_1_STACK
    1,                               // BC_RETURN_LOCAL_STACK
    1,                               // BC_RETURN_SELF_STACK
    1,                               // BC_RETURN_FIELD_0_STACK
    1,                               // BC_RETURN_FIELD_1_STACK
    1,                               // BC_RETURN_FIELD_2_STACK
};

const char* Bytecode::bytecodeNames[] = {
    "HALT            ",          // 0
    "DUP             ",          // 1
    "DUP_SECOND      ",          // 2
    "PUSH_LOCAL      ",          // 3
    "PUSH_LOCAL_0    ",          // 4
    "PUSH_LOCAL_1    ",          // 5
    "PUSH_LOCAL_2    ",          // 6
    "PUSH_ARGUMENT   ",          // 7
    "PUSH_SELF       ",          // 8
    "PUSH_ARG_1      ",          // 9
    "PUSH_ARG_2      ",          // 10
    "PUSH_FIELD      ",          // 11
    "PUSH_FIELD_0    ",          // 12
    "PUSH_FIELD_1    ",          // 13
    "PUSH_BLOCK      ",          // 14
    "PUSH_CONSTANT   ",          // 15
    "PUSH_0          ",          // 16
    "PUSH_1          ",          // 17
    "PUSH_SMALL_INT  ",          // 18
    "PUSH_NIL        ",          // 19
    "PUSH_GLOBAL     ",          // 20
    "POP             ",          // 21
    "POP_LOCAL       ",          // 22
    "POP_LOCAL_0     ",          // 23
    "POP_LOCAL_1     ",          // 24
    "POP_LOCAL_2     ",          // 25
    "POP_ARGUMENT    ",          // 26
    "POP_FIELD       ",          // 27
    "POP_FIELD_0     ",          // 28
    "POP_FIELD_1     ",          // 29
    "SEND            ",          // 30
    "SEND_1          ",          // 31
    "SUPER_SEND      ",          // 32
    "RETURN_LOCAL    ",          // 33
    "RETURN_NON_LOCAL",          // 34
    "RETURN_SELF     ",          // 35
    "RETURN_FIELD_0  ",          // 36
    "RETURN_FIELD_1  ",          // 37
    "RETURN_FIELD_2  ",          // 38
    "INC             ",          // 39
    "DEC             ",          // 40
    "INC_FIELD       ",          // 41
    "INC_FIELD_PUSH  ",          // 42
    "JUMP            ",          // 43
    "JUMP_ON_FALSE_POP",         // 44
    "JUMP_ON_TRUE_POP",          // 45
    "JUMP_ON_FALSE_TOP_NIL",     // 46
    "JUMP_ON_TRUE_TOP_NIL",      // 47
    "JUMP_ON_NOT_NIL_POP",       // 48
    "JUMP_ON_NIL_POP ",          // 49
    "JUMP_ON_NOT_NIL_TOP_TOP",   // 50
    "JUMP_ON_NIL_TOP_TOP",       // 51
    "JUMP_IF_GREATER ",          // 52
    "JUMP_BACKWARD   ",          // 53
    "JUMP2           ",          // 54
    "JUMP2_ON_FALSE_POP",        // 55
    "JUMP2_ON_TRUE_POP",         // 56
    "JUMP2_ON_FALSE_TOP_NIL",    // 57
    "JUMP2_ON_TRUE_TOP_NIL",     // 58
    "JUMP2_ON_NOT_NIL_POP",      // 59
    "JUMP2_ON_NIL_POP ",         // 60
    "JUMP2_ON_NOT_NIL_TOP_TOP",  // 61
    "JUMP2_ON_NIL_TOP_TOP",      // 62
    "JUMP2_IF_GREATER",          // 63
    "JUMP2_BACKWARD  ",          // 64
    "SEND_EQEQ       ",          // 65
    "STATIC_SEND     ",          // 66
    "STATIC_SEND_1   ",          // 67
    "NOT_UNDERSTOOD  ",          // 68
    "PUSH_STACK      ",          // 69
    "POP_STACK       ",          // 70
    "SEND_STACK      ",          // 71
    "SEND_1_STACK    ",          // 72
    "SEND_EQEQ_STACK ",          // 73
    "STATIC_SEND_STACK",         // 74
    "STATIC_SEND_1_STACK",       // 75
    "RETURN_LOCAL_STACK",        // 76
    "RETURN_SELF_STACK",         // 77
    "RETURN_FIELD_0_STACK",      // 78
    "RETURN_FIELD_1_STACK",      // 79
    "RETURN_FIELD_2_STACK",      // 80
};

uint8_t GetNumberOfWords(uint8_t bc) {
    switch (bc) {
        case BC_PUSH_BLOCK:
        case BC_PUSH_CONSTANT:
        case BC_PUSH_GLOBAL:
        case BC_STATIC_SEND:
        case BC_STATIC_SEND_1:
        case BC_NOT_UNDERSTOOD:
        case BC_STATIC_SEND_STACK:
        case BC_STATIC_SEND_1_STACK:
            return 1;
        case BC_SEND:
        case BC_SEND_1:
        case BC_SUPER_SEND:
        case BC_SEND_EQEQ:
        case BC_SEND_STACK:
        case BC_SEND_1_STACK:
        case BC_SEND_EQEQ_STACK:
            return 2;
        default:
            return 0;
    }
}

bool IsSendBytecode(uint8_t bc) {
    return bc == BC_SEND || bc == BC_SEND_1 || bc == BC_SUPER_SEND ||
           bc == BC_SEND_EQEQ;
}

bool IsJumpBytecode(uint8_t bc) {
    static_assert(
        BC_JUMP < BC_JUMP2_BACKWARD,
        "make sure the nummeric value of jump bytecodes is as expected");
    static_assert((BC_JUMP2_BACKWARD - BC_JUMP) == 21,
                  "we expect there to be 22 jump bytecodes");

    return BC_JUMP <= bc && bc <= BC_JUMP2_BACKWARD;
}

uint8_t IsPushConstBytecode(uint8_t bc) {
    switch (bc) {
        case BC_PUSH_CONSTANT:
        case BC_PUSH_0:
        case BC_PUSH_1:
        case BC_PUSH_SMALL_INT:
        case BC_PUSH_NIL:
            return bc;

        default:
            return BC_INVALID;
    }
}

uint8_t IsPushArgBytecode(uint8_t bc) {
    switch (bc) {
        case BC_PUSH_SELF:
        case BC_PUSH_ARG_1:
        case BC_PUSH_ARG_2:
        case BC_PUSH_ARGUMENT:
            return bc;

        default:
            return BC_INVALID;
    }
}

uint8_t IsPushFieldBytecode(uint8_t bc) {
    switch (bc) {
        case BC_PUSH_FIELD:
        case BC_PUSH_FIELD_0:
        case BC_PUSH_FIELD_1:
            return bc;

        default:
            return BC_INVALID;
    }
}

uint8_t IsPopFieldBytecode(uint8_t bc) {
    switch (bc) {
        case BC_POP_FIELD:
        case BC_POP_FIELD_0:
        case BC_POP_FIELD_1:
            return bc;

        default:
            return BC_INVALID;
    }
}

uint8_t IsReturnFieldBytecode(uint8_t bc) {
    switch (bc) {
        case BC_RETURN_FIELD_0:
        case BC_RETURN_FIELD_1:
        case BC_RETURN_FIELD_2:
            return bc;

        default:
            return BC_INVALID;
    }
}

uint8_t IsPopSmthBytecode(uint8_t bc) {
    switch (bc) {
        case BC_POP_LOCAL:
        case BC_POP_LOCAL_0:
        case BC_POP_LOCAL_1:
        case BC_POP_LOCAL_2:
        case BC_POP_ARGUMENT:
        case BC_POP_FIELD:
        case BC_POP_FIELD_0:
        case BC_POP_FIELD_1:
            return bc;

        default:
            return BC_INVALID;
    }
}

bool Bytecode::BytecodeDefinitionsAreConsistent() {
    bool const namesAndLengthMatch =
        (sizeof(Bytecode::bytecodeNames) / sizeof(char*)) ==
        (sizeof(Bytecode::bytecodeLengths) / sizeof(uint8_t));
    bool const lastBytecodeLinesUp =
        _LAST_BYTECODE ==
        (sizeof(Bytecode::bytecodeLengths) - 1);  // -1 because null terminated

    return namesAndLengthMatch && lastBytecodeLinesUp;
}

#ifndef FRUCTUS
std::vector<int> ComputeStackDepths(const uint8_t* bytecodes, size_t length) {
    std::vector<int> depths(length, -1);
    // the indices to go on from, whose depths are known
    std::vector<size_t> work;
    auto reach = [&](size_t i, int depth) {
        assert(i < length && depth >= 0);
        if (depths[i] == -1) {
            depths[i] = depth;
            work.push_back(i);
        } else if (depths[i] != depth) {
            ErrorExit("A bytecode is reached at two stack depths");
        }
    };
    if (length > 0) {
        reach(0, 0);
    }

    while (!work.empty()) {
        size_t const i = work.back();
        work.pop_back();
        uint8_t const bc = bytecodes[i];
        int const depth = depths[i];
        size_t const next = i + Bytecode::GetBytecodeLength(bc);

        if (IsJumpBytecode(bc)) {
            bool const isLong = bc >= FIRST_DOUBLE_BYTE_JUMP_BYTECODE;
            size_t const offset =
                isLong ? ComputeOffset(bytecodes[i + 1], bytecodes[i + 2])
                       : bytecodes[i + 1];
            uint8_t const shortBc =
                isLong ? bc - NUM_SINGLE_BYTE_JUMP_BYTECODES : bc;
            // the depth after the jump if it jumps, and if it doesn't
            int taken = depth;
            int notTaken = depth;
            switch (shortBc) {
                case BC_JUMP:
                case BC_JUMP_BACKWARD:
                    notTaken = -1;
                    break;
                case BC_JUMP_ON_FALSE_POP:
                case BC_JUMP_ON_TRUE_POP:
                case BC_JUMP_ON_NOT_NIL_POP:
                case BC_JUMP_ON_NIL_POP:
                    taken = depth - 1;
                    notTaken = depth - 1;
                    break;
                case BC_JUMP_ON_FALSE_TOP_NIL:
                case BC_JUMP_ON_TRUE_TOP_NIL:
                case BC_JUMP_ON_NOT_NIL_TOP_TOP:
                case BC_JUMP_ON_NIL_TOP_TOP:
                    notTaken = depth - 1;
                    break;
                case BC_JUMP_IF_GREATER:
                    taken = depth - 2;
                    break;
                default:
                    ErrorExit("Unexpected jump bytecode");
            }
            reach(shortBc == BC_JUMP_BACKWARD ? i - offset : i + offset, taken);
            if (notTaken != -1) {
                reach(next, notTaken);
            }
            continue;
        }

        int effect = 0;
        switch (bc) {
            case BC_HALT:
            case BC_RETURN_LOCAL:
            case BC_RETURN_NON_LOCAL:
            case BC_RETURN_SELF:
            case BC_RETURN_FIELD_0:
            case BC_RETURN_FIELD_1:
            case BC_RETURN_FIELD_2:
                continue;
            case BC_DUP:
            case BC_DUP_SECOND:
            case BC_PUSH_LOCAL:
            case BC_PUSH_LOCAL_0:
            case BC_PUSH_LOCAL_1:
            case BC_PUSH_LOCAL_2:
            case BC_PUSH_ARGUMENT:
            case BC_PUSH_SELF:
            case BC_PUSH_ARG_1:
            case BC_PUSH_ARG_2:
            case BC_PUSH_FIELD:
            case BC_PUSH_FIELD_0:
            case BC_PUSH_FIELD_1:
            case BC_PUSH_BLOCK:
            case BC_PUSH_CONSTANT:
            case BC_PUSH_0:
            case BC_PUSH_1:
            case BC_PUSH_SMALL_INT:
            case BC_PUSH_NIL:
            case BC_PUSH_GLOBAL:
            case BC_INC_FIELD_PUSH:
            case BC_PUSH_STACK:
                effect = 1;
                break;
            case BC_POP:
            case BC_POP_LOCAL:
            case BC_POP_LOCAL_0:
            case BC_POP_LOCAL_1:
            case BC_POP_LOCAL_2:
            case BC_POP_ARGUMENT:
            case BC_POP_FIELD:
            case BC_POP_FIELD_0:
            case BC_POP_FIELD_1:
            case BC_POP_STACK:
                effect = -1;
                break;
            case BC_INC:
            case BC_DEC:
            case BC_INC_FIELD:
                break;
            case BC_SEND:
            case BC_SEND_1:
            case BC_SUPER_SEND:
            case BC_SEND_EQEQ:
            case BC_NOT_UNDERSTOOD:
                // the receiver and arguments become the result
                effect = 1 - SendSite::GetNumberOfArguments(&bytecodes[i]);
                break;
            default:
                ErrorExit("Unexpected bytecode in ComputeStackDepths");
        }
        reach(next, depth + effect);
    }
    return depths;
}
#endif
