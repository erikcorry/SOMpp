#include "TestWithParsing.h"

#include <algorithm>
#include <cassert>
#include <cppunit/TestAssert.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "../compiler/ClassGenerationContext.h"
#include "../compiler/Disassembler.h"
#include "../compiler/MethodGenerationContext.h"
#include "../compiler/Parser.h"
#include "../interpreter/bytecodes.h"
#include "../vm/Symbols.h"
#include "../vmobjects/VMMethod.h"

void TestWithParsing::dump(MethodGenerationContext* mgenc) {
    if (mgenc == nullptr) {
        if (_bgenc != nullptr) {
            mgenc = _bgenc;
        } else {
            mgenc = _mgenc;
        }
    }
    Disassembler::DumpMethod(mgenc, "");
}

void TestWithParsing::dump(VMMethod* method) {
    if (method != nullptr) {
        Disassembler::DumpMethod(method, "");
        return;
    }

    dump(static_cast<MethodGenerationContext*>(nullptr));
}

void TestWithParsing::ensureCGenC() {
    if (_cgenc != nullptr) {
        return;
    }

    _cgenc = new ClassGenerationContext();
    _cgenc->SetName(SymbolFor("Test"));
}

void TestWithParsing::ensureMGenC() {
    if (_mgenc != nullptr) {
        return;
    }
    ensureCGenC();

    _mgenc = new MethodGenerationContext(*_cgenc);
    std::string self = strSelf;
    _mgenc->AddArgument(self, {0, 0}, nullptr);
}

void TestWithParsing::ensureBGenC() {
    if (_bgenc != nullptr) {
        return;
    }
    ensureCGenC();
    ensureMGenC();

    _mgenc->SetSignature(SymbolFor("test"));
    _bgenc = new MethodGenerationContext(*_cgenc, _mgenc);
}

void TestWithParsing::addField(const char* fieldName) {
    ensureCGenC();
    _cgenc->AddInstanceField(SymbolFor(fieldName));
}

std::vector<uint8_t> TestWithParsing::methodToBytecode(const char* source,
                                                       bool dumpBytecodes) {
    ensureMGenC();

    istringstream ss(source);

    std::string fileName = "test";
    Parser parser(ss, fileName);
    parser.method(*_mgenc);

    if (dumpBytecodes) {
        dump(_mgenc);
    }
    return _mgenc->GetBytecodes();
}

std::vector<uint8_t> TestWithParsing::blockToBytecode(const char* source,
                                                      bool dumpBytecodes) {
    ensureBGenC();

    istringstream ss(source);

    std::string fileName = "test";
    Parser parser(ss, fileName);

    parser.nestedBlock(*_bgenc);

    if (dumpBytecodes) {
        dump(_bgenc);
    }
    return _bgenc->GetBytecodes();
}

/** The single byte variant of a jump, which only differs in its range. */
static uint8_t singleByteJump(uint8_t bc) {
    if (bc >= FIRST_DOUBLE_BYTE_JUMP_BYTECODE && bc <= BC_JUMP2_BACKWARD) {
        return bc - NUM_SINGLE_BYTE_JUMP_BYTECODES;
    }
    return bc;
}

/**
 * The index of the bytecode that the jump at `index` goes to, given where
 * each bytecode starts, or SIZE_MAX if it lands within one.
 */
static size_t jumpTarget(const std::vector<size_t>& starts, size_t index,
                         uint8_t bc, uint16_t offset) {
    size_t const target = singleByteJump(bc) == BC_JUMP_BACKWARD
                              ? starts.at(index) - offset
                              : starts.at(index) + offset;
    for (size_t i = 0; i < starts.size(); i++) {
        if (starts.at(i) == target) {
            return i;
        }
    }
    return SIZE_MAX;
}

/*
 * The bytecodes with word operands (constants, blocks, globals and sends)
 * had a byte operand before, the index of a literal, which the expectations
 * still have. So for those only the bytecode is compared. The offset of an
 * expected jump counts the sizes of the expected bytecodes, so what's
 * compared for a jump is the bytecode it goes to.
 */
void TestWithParsing::check(std::vector<uint8_t> actual,
                            std::vector<BC> expected, VMMethod* toDump) {
    // where each bytecode starts, and where the code ends
    std::vector<size_t> actualStarts;
    size_t bci = 0;
    for (; bci < actual.size();
         bci += Bytecode::GetBytecodeLength(actual.at(bci))) {
        actualStarts.push_back(bci);
    }
    actualStarts.push_back(bci);
    std::vector<size_t> expectedStarts;
    size_t at = 0;
    for (const BC& bc : expected) {
        expectedStarts.push_back(at);
        at += bc.size;
    }
    expectedStarts.push_back(at);

    size_t const count = std::min(expected.size(), actualStarts.size() - 1);
    for (size_t i = 0; i < count; i++) {
        size_t const bci = actualStarts.at(i);
        uint8_t const actualBc = actual.at(bci);
        uint8_t const bcLength = Bytecode::GetBytecodeLength(actualBc);
        BC const expectedBc = expected.at(i);
        bool const isJump = IsJumpBytecode(actualBc);

        char msg[1000];
        (void)snprintf(msg, 1000,
                       "Bytecode no %zu (at: %zu) expected %s but got %s", i,
                       bci, Bytecode::GetBytecodeName(expectedBc.bytecode),
                       Bytecode::GetBytecodeName(actualBc));
        uint8_t const expectedOp =
            isJump ? singleByteJump(expectedBc.bytecode) : expectedBc.bytecode;
        uint8_t const actualOp = isJump ? singleByteJump(actualBc) : actualBc;
        if (expectedOp != actualOp) {
            dump(toDump);
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, expectedOp, actualOp);

        if (GetNumberOfWords(actualBc) > 0) {
            continue;
        }

        if (isJump) {
            size_t const expectedTarget =
                jumpTarget(expectedStarts, i, expectedBc.bytecode,
                           ComputeOffset(expectedBc.arg1, expectedBc.arg2));
            size_t const actualTarget = jumpTarget(
                actualStarts, i, actualBc,
                ComputeOffset(actual.at(bci + 1), actual.at(bci + 2)));
            (void)snprintf(msg, 1000,
                           "Bytecode %zu (%s) expected to jump to bytecode "
                           "%zu but jumps to %zu",
                           i, Bytecode::GetBytecodeName(actualBc),
                           expectedTarget, actualTarget);
            if (expectedTarget != actualTarget) {
                dump(toDump);
            }
            CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, expectedTarget, actualTarget);
            continue;
        }

        (void)snprintf(
            msg, 1000,
            "Bytecode %zu (%s) was expected to have length %zu, but had %zu", i,
            Bytecode::GetBytecodeName(actualBc), expectedBc.size,
            (size_t)bcLength);
        if (expectedBc.size != bcLength) {
            dump(toDump);
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, expectedBc.size, (size_t)bcLength);

        if (bcLength > 1) {
            (void)snprintf(msg, 1000,
                           "Bytecode %zu (%s), arg1 expected %hhu but got %hhu",
                           i, Bytecode::GetBytecodeName(expectedBc.bytecode),
                           expectedBc.arg1, actual.at(bci + 1));
            if (expectedBc.arg1 != actual.at(bci + 1)) {
                dump(toDump);
            }
            CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, expectedBc.arg1,
                                         actual.at(bci + 1));

            if (bcLength > 2) {
                (void)snprintf(
                    msg, 1000,
                    "Bytecode %zu (%s), arg2 expected %hhu but got %hhu", i,
                    Bytecode::GetBytecodeName(expectedBc.bytecode),
                    expectedBc.arg2, actual.at(bci + 2));
                if (expectedBc.arg2 != actual.at(bci + 2)) {
                    dump(toDump);
                }
                CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, expectedBc.arg2,
                                             actual.at(bci + 2));
            }
        }
    }
    if (expected.size() != actualStarts.size() - 1) {
        dump(toDump);
    }
    CPPUNIT_ASSERT_EQUAL_MESSAGE("number of bytecodes", expected.size(),
                                 actualStarts.size() - 1);
}
