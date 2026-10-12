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

#include "VMMethod.h"

#include <assert.h>
#include <cstdint>
#include <stdio.h>
#include <string.h>
#ifndef FRUCTUS
#include <queue>
#endif

#ifndef FRUCTUS
#include "../compiler/BytecodeGenerator.h"
#include "../compiler/Disassembler.h"
#endif
#ifndef FRUCTUS
#include "../compiler/LexicalScope.h"
#endif
#ifndef FRUCTUS
#include "../compiler/MethodGenerationContext.h"
#endif
#ifndef FRUCTUS
#include "../compiler/Variable.h"
#endif
#include "../interpreter/Interpreter.h"
#include "../interpreter/SendSite.h"
#include "../interpreter/bytecodes.h"
#include "../memory/Heap.h"
#ifndef FRUCTUS
#include "../misc/Murmur3Hash.h"
#endif
#include "../misc/defs.h"
#include "../vm/Globals.h"
#include "../vm/IsValidObject.h"
#include "../vm/Print.h"
#include "../vm/Universe.h"  // NOLINT(misc-include-cleaner) it's required to make the types complete
#include "ObjectFormats.h"
#include "Signature.h"
#include "VMClass.h"
#include "VMFrame.h"
#include "VMObject.h"
#include "VMSymbol.h"

#ifdef FRUCTUS
VMMethod::VMMethod(VMSymbol* signature, size_t bcCount, size_t numLocals,
                   size_t maxStackDepth, uint8_t numArgs)
    : VMInvokable(signature), bcLength(bcCount), numberOfLocals(numLocals),
      maximumNumberOfStackElements(maxStackDepth), numberOfArguments(numArgs) {
#else
VMMethod::VMMethod(VMSymbol* signature, size_t bcCount, size_t numLocals,
                   size_t maxStackDepth, LexicalScope* lexicalScope,
                   BackJump* inlinedLoops)
    : VMInvokable(signature), numberOfLocals(numLocals),
      maximumNumberOfStackElements(maxStackDepth), bcLength(bcCount),
      numberOfArguments(signature == nullptr
                            ? 0
                            : Signature::GetNumberOfArguments(signature)),
      lexicalScope(lexicalScope), inlinedLoops(inlinedLoops) {
#endif
#ifdef UNSAFE_FRAME_OPTIMIZATION
    cachedFrame = nullptr;
#endif

#ifdef BYTECODE_HEATMAP
    heatmap = new uint64_t[bcCount]();
#endif
}

VMMethod* VMMethod::CloneForMovingGC() const {
    auto* clone =
        new (GetHeap<HEAP_CLS>(),
             GetObjectSize() - sizeof(VMMethod) ALLOC_MATURE) VMMethod(*this);
    memcpy(SHIFTED_PTR(clone, sizeof(VMObject)),
           SHIFTED_PTR(this, sizeof(VMObject)),
           GetObjectSize() - sizeof(VMObject));
    return clone;
}

void VMMethod::WalkObjects(walk_heap_fn walk) {
    VMInvokable::WalkObjects(walk);

#ifdef UNSAFE_FRAME_OPTIMIZATION
    if (cachedFrame != nullptr) {
        cachedFrame = static_cast<GCFrame*>(walk(cachedFrame));
    }
#endif

#ifndef FRUCTUS
    // the words in the bytecodes, see bytecodes.h
    uint8_t* bytecodes = GetBytecodes();
    for (size_t i = 0; i < bcLength;
         i += Bytecode::GetBytecodeLength(bytecodes[i])) {
        uint8_t const bc = bytecodes[i];
        uint8_t const numWords = GetNumberOfWords(bc);
        uint8_t* word = &bytecodes[i] + Bytecode::GetBytecodeLength(bc) -
                        numWords * sizeof(void*);
        for (uint8_t w = 0; w < numWords; w++, word += sizeof(void*)) {
            // an unlinked send has no receiverClass yet
            if (LoadWord(word) != nullptr) {
                StoreWord(word, walk((gc_oop_t)LoadWord(word)));
            }
        }
    }
#endif
}

#ifndef FRUCTUS
VMSymbol* VMMethod::GetSendSelector(size_t bytecodeIndex) const {
    const uint8_t* site = &GetBytecodes()[bytecodeIndex];
    assert(IsSendBytecode(site[0]));
    void* target = SendSite::GetTarget(site);
    if (SendSite::GetReceiverClass(site) == nullptr) {
        return (VMSymbol*)target;
    }
    return ((VMInvokable*)target)->GetSignature();
}
#endif

#ifdef UNSAFE_FRAME_OPTIMIZATION
GCFrame* VMMethod::GetCachedFrame() const {
    return cachedFrame;
}

void VMMethod::SetCachedFrame(VMFrame* frame) {
    cachedFrame = store_with_separate_barrier(frame);
    if (frame != nullptr) {
        frame->SetContext(nullptr);
        frame->SetBytecodeIndex(0);
        frame->ResetStackPointer();
        write_barrier(this, frame);
    }
}
#endif

// Answers nullptr, and changes nothing, if the frame can't be allocated.
VMFrame* VMMethod::Invoke(VMFrame* frame) {
    VMFrame* frm = GetInterpreter()->PushNewFrame(this);
    if (frm != nullptr) {
        frm->CopyArgumentsFrom(frame);
    }
    return frm;
}

VMFrame* VMMethod::Invoke1(VMFrame* frame) {
    VMFrame* frm = GetInterpreter()->PushNewFrame(this);
    if (frm != nullptr) {
        frm->SetArgumentOfCurrent(0, frame->Top());
    }
    return frm;
}

#ifndef FRUCTUS
void VMMethod::SetHolder(VMClass* hld) {
    VMInvokable::SetHolder(hld);
    SetHolderAll(hld);
}

void VMMethod::SetHolderAll(VMClass* hld) const {
    for (size_t i = 0; i < bcLength;
         i += Bytecode::GetBytecodeLength(GetBytecode(i))) {
        if (GetBytecode(i) == BC_PUSH_BLOCK) {
            static_cast<VMInvokable*>(GetConstant(i))->SetHolder(hld);
        }
    }
}
#endif

#ifndef FRUCTUS
void VMMethod::Dump(const char* indent, bool printObjects) {
    Disassembler::DumpMethod(this, indent, printObjects);
}
#endif

#if DEBUG
std::string VMMethod::AsDebugString() const {
    VMClass const* const holder = GetHolder();
    std::string holder_str;
    if (holder == load_ptr(nilObject)) {
        holder_str = "nil";
    } else {
        holder_str = holder->GetName()->GetStdString();
    }
    return "Method(" + holder_str + ">>#" + GetSignature()->GetStdString() +
           ")";
}
#endif

#ifndef FRUCTUS
void VMMethod::InlineInto(MethodGenerationContext& mgenc, const Parser& parser,
                          bool mergeScope) {
    if (mergeScope) {
        mgenc.MergeIntoScope(*lexicalScope);
    }
    inlineInto(mgenc, parser);
}

std::priority_queue<BackJump> VMMethod::createBackJumpHeap() {
    std::priority_queue<BackJump> loops;
    if (inlinedLoops != nullptr) {
        size_t i = 0;
        while (inlinedLoops[i].IsValid()) {
            loops.emplace(inlinedLoops[i]);
            i += 1;
        }
    }
    return loops;
}

void VMMethod::inlineInto(MethodGenerationContext& mgenc,
                          const Parser& parser) {
    std::priority_queue<Jump>
        jumps;  // priority queue sorted by originalJumpTargetIdx
    std::priority_queue<BackJump> backJumps = createBackJumpHeap();
    std::priority_queue<BackJumpPatch> backJumpsToPatch;

    uint8_t* bytecodes = GetBytecodes();
    size_t i = 0;
    const size_t numBytecodes = GetNumberOfBytecodes();
    // the bytecodes keep their lines in the method they're inlined into
    uint16_t const outerLine = mgenc.SetLine(GetLine(0));
    while (i < numBytecodes) {
        prepareBackJumpToCurrentAddress(backJumps, backJumpsToPatch, i, mgenc);
        patchJumpToCurrentAddress(i, jumps, mgenc);
        mgenc.SetLine(GetLine(i));

        const uint8_t bytecode = bytecodes[i];
        const uint8_t bcLength = Bytecode::GetBytecodeLength(bytecode);

        switch (bytecode) {
            case BC_DUP:
            case BC_DUP_SECOND: {
                Emit1(mgenc, bytecode, 1);
                break;
            }
            case BC_PUSH_FIELD:
            case BC_PUSH_FIELD_0:
            case BC_PUSH_FIELD_1:
            case BC_POP_FIELD:
            case BC_POP_FIELD_0:
            case BC_POP_FIELD_1: {
                uint8_t idx = 0;
                if (bytecode == BC_PUSH_FIELD || bytecode == BC_POP_FIELD) {
                    idx = bytecodes[i + 1];
                } else {
                    switch (bytecode) {
                        case BC_PUSH_FIELD_0: {
                            idx = 0;
                            break;
                        }
                        case BC_PUSH_FIELD_1: {
                            idx = 1;
                            break;
                        }
                        case BC_POP_FIELD_0: {
                            idx = 0;
                            break;
                        }
                        case BC_POP_FIELD_1: {
                            idx = 1;
                            break;
                        }
                        default: {
                            ErrorExit("Unexpected bytecode in inlineInto");
                        }
                    }
                }

                if (bytecode == BC_PUSH_FIELD || bytecode == BC_PUSH_FIELD_0 ||
                    bytecode == BC_PUSH_FIELD_1) {
                    EmitPushFieldWithIndex(mgenc, idx);
                } else {
                    EmitPopFieldWithIndex(mgenc, idx);
                }
                break;
            }

            case BC_POP_ARGUMENT: {
                const uint8_t idx = bytecodes[i + 1];
                const uint8_t ctxLevel = bytecodes[i + 2];
                assert(ctxLevel > 0);

                assert(bytecode == BC_POP_ARGUMENT);
                EmitPOPARGUMENT(mgenc, parser, idx, ctxLevel - 1);
                break;
            }
            case BC_PUSH_ARGUMENT: {
                const uint8_t idx = bytecodes[i + 1];
                const uint8_t ctxLevel = bytecodes[i + 2];
                assert(ctxLevel > 0);

                EmitPUSHARGUMENT(mgenc, parser, idx, ctxLevel - 1);
                break;
            }
            case BC_INC_FIELD:
            case BC_INC_FIELD_PUSH: {
                const uint8_t idx = bytecodes[i + 1];
                Emit2(mgenc, bytecode, idx, bytecode == BC_INC_FIELD ? 0 : 1);
                break;
            }
            case BC_PUSH_LOCAL:
            case BC_POP_LOCAL: {
                uint8_t idx = bytecodes[i + 1];
                uint8_t ctxLevel = bytecodes[i + 2];

                if (ctxLevel == 0) {
                    // these have been inlined into the outer context already
                    // so, we need to look up the right one
                    const Variable* const var = lexicalScope->GetLocal(idx, 0);
                    idx = mgenc.GetInlinedLocalIdx(var);
                } else {
                    ctxLevel -= 1;
                }

                if (bytecode == BC_PUSH_LOCAL) {
                    EmitPUSHLOCAL(mgenc, parser, idx, ctxLevel);
                } else {
                    EmitPOPLOCAL(mgenc, parser, idx, ctxLevel);
                }
                break;
            }

            case BC_PUSH_LOCAL_0:
            case BC_PUSH_LOCAL_1:
            case BC_PUSH_LOCAL_2: {
                uint8_t const idx = bytecode - BC_PUSH_LOCAL_0;
                const auto* oldVar = lexicalScope->GetLocal(idx, 0);
                uint8_t const newIdx = mgenc.GetInlinedLocalIdx(oldVar);
                EmitPUSHLOCAL(mgenc, parser, newIdx, 0);
                break;
            }

            case BC_POP_LOCAL_0:
            case BC_POP_LOCAL_1:
            case BC_POP_LOCAL_2: {
                uint8_t const idx = bytecode - BC_POP_LOCAL_0;
                const auto* oldVar = lexicalScope->GetLocal(idx, 0);
                uint8_t const newIdx = mgenc.GetInlinedLocalIdx(oldVar);
                EmitPOPLOCAL(mgenc, parser, newIdx, 0);
                break;
            }

            case BC_PUSH_ARG_1:
            case BC_PUSH_ARG_2: {
                // this can now happen with inlining #to:do:
                size_t const argIdx = bytecode == BC_PUSH_ARG_1 ? 1 : 2;

                const Variable* arg = lexicalScope->GetArgument(argIdx, 0);
                size_t const inlinedLocalIndex = mgenc.GetInlinedLocalIdx(arg);
                EmitPUSHLOCAL(mgenc, parser, inlinedLocalIndex, 0);
                break;
            }

            case BC_PUSH_BLOCK: {
                auto* blockMethod = (VMInvokable*)GetConstant(i);
                blockMethod->AdaptAfterOuterInlined(1, mgenc);
                EmitPUSHBLOCK(mgenc, parser, blockMethod);
                break;
            }
            case BC_PUSH_CONSTANT: {
                vm_oop_t literal = GetConstant(i);
                EmitPUSHCONSTANT(mgenc, parser, literal);
                break;
            }
            case BC_PUSH_0:
            case BC_PUSH_1:
            case BC_PUSH_NIL: {
                Emit1(mgenc, bytecode, 1);
                break;
            }
            case BC_PUSH_SMALL_INT: {
                Emit2(mgenc, bytecode, bytecodes[i + 1], 1);
                break;
            }
            case BC_POP: {
                // TODO(smarr): PySOM simply does Emit1
                //   not sure whether EmitPOP might cause issues if we try to do
                //   optimizations here again
                EmitPOP(mgenc);
                break;
            }
            case BC_INC:
            case BC_DEC: {
                Emit1(mgenc, bytecode, 0);
                break;
            }
            case BC_PUSH_GLOBAL: {
                auto* const sym = (VMSymbol*)GetConstant(i);
                EmitPUSHGLOBAL(mgenc, parser, sym);
                break;
            }
            case BC_SEND:
            case BC_SEND_1:
            case BC_SEND_2:
            case BC_SEND_3:
            case BC_SEND_N:
            case BC_SEND_EQEQ: {
                VMSymbol* const sym = GetSendSelector(i);
                EmitSEND(mgenc, parser, sym);
                break;
            }
            case BC_SUPER_SEND: {
                VMSymbol* const sym = GetSendSelector(i);
                EmitSUPERSEND(mgenc, parser, sym);
                break;
            }
            case BC_RETURN_LOCAL: {
                // NO OP, doesn't need to be translated
                break;
            }
            case BC_RETURN_NON_LOCAL: {
                if (mgenc.IsBlockMethod()) {
                    assert(mgenc.GetMaxContextLevel() > 0);
                    EmitRETURNNONLOCAL(mgenc);
                } else {
                    EmitRETURNLOCAL(mgenc, parser);
                }
                break;
            }
            case BC_RETURN_FIELD_0:
            case BC_RETURN_FIELD_1:
            case BC_RETURN_FIELD_2: {
                uint8_t const index = bytecode - BC_RETURN_FIELD_0;
                EmitPushFieldWithIndex(mgenc, index);
                break;
            }

            case BC_JUMP:
            case BC_JUMP_ON_TRUE_TOP_NIL:
            case BC_JUMP_ON_FALSE_TOP_NIL:
            case BC_JUMP_ON_NOT_NIL_TOP_TOP:
            case BC_JUMP_ON_NIL_TOP_TOP:
            case BC_JUMP2:
            case BC_JUMP2_ON_TRUE_TOP_NIL:
            case BC_JUMP2_ON_FALSE_TOP_NIL:
            case BC_JUMP2_ON_NOT_NIL_TOP_TOP:
            case BC_JUMP2_ON_NIL_TOP_TOP:
            case BC_JUMP_IF_GREATER:
            case BC_JUMP2_IF_GREATER: {
                // emit the jump, but instead of the offset, emit a dummy
                const size_t idx = Emit3WithDummy(mgenc, bytecode, 0);
                const size_t offset =
                    ComputeOffset(bytecodes[i + 1], bytecodes[i + 2]);

                jumps.emplace(i + offset, bytecode, idx);
                break;
            }
            case BC_JUMP_ON_TRUE_POP:
            case BC_JUMP_ON_FALSE_POP:
            case BC_JUMP2_ON_TRUE_POP:
            case BC_JUMP2_ON_FALSE_POP:
            case BC_JUMP_ON_NOT_NIL_POP:
            case BC_JUMP_ON_NIL_POP:
            case BC_JUMP2_ON_NOT_NIL_POP:
            case BC_JUMP2_ON_NIL_POP: {
                // emit the jump, but instead of the offset, emit a dummy
                const size_t idx = Emit3WithDummy(mgenc, bytecode, -1);
                const size_t offset =
                    ComputeOffset(bytecodes[i + 1], bytecodes[i + 2]);
                jumps.emplace(i + offset, bytecode, idx);
                break;
            }
            case BC_JUMP_BACKWARD:
            case BC_JUMP2_BACKWARD: {
                const size_t loopBeginIdx = backJumpsToPatch.top().loopBeginIdx;
                assert(backJumpsToPatch.top().backwardsJumpIdx == i &&
                       "the jump should match with the jump instructions");

                backJumpsToPatch.pop();
                mgenc.EmitBackwardsJumpOffsetToTarget(loopBeginIdx);
                break;
            }

            case BC_HALT:
            case BC_PUSH_SELF:
            case BC_RETURN_SELF: {
                char msg[120];
                (void)snprintf(
                    msg, 120,
                    "inlineInto: Found %s bytecode, but it's not expected in a "
                    "block method",
                    Bytecode::GetBytecodeName(bytecode));
                ErrorExit(msg);
                break;
            }
            default: {
                char msg[120];
                (void)snprintf(
                    msg, 120,
                    "inlineInto: Found %s bytecode, but inlining of it is "
                    "not yet "
                    "supported.",
                    Bytecode::GetBytecodeName(bytecode));
                ErrorExit(msg);
                break;
            }
        }

        i += bcLength;
    }
    mgenc.SetLine(outerLine);

    assert(jumps.empty());
}

void VMMethod::patchJumpToCurrentAddress(size_t i,
                                         std::priority_queue<Jump>& jumps,
                                         MethodGenerationContext& mgenc) {
    while (!jumps.empty() && jumps.top().originalJumpTargetIdx <= i) {
        Jump const jump = jumps.top();
        jumps.pop();

        assert(
            jump.originalJumpTargetIdx == i &&
            "we use the less or equal, but actually expect it to be strictly "
            "equal");
        mgenc.PatchJumpOffsetToPointToNextInstruction(jump.idx);
    }
}

void VMMethod::prepareBackJumpToCurrentAddress(
    std::priority_queue<BackJump>& backJumps,
    std::priority_queue<BackJumpPatch>& backJumpsToPatch, size_t i,
    MethodGenerationContext& mgenc) {
    while (!backJumps.empty() && backJumps.top().loopBeginIdx <= i) {
        BackJump const jump = backJumps.top();
        backJumps.pop();

        assert(
            jump.loopBeginIdx == i &&
            "we use the less or equal, but actually expect it to be strictly "
            "equal");
        backJumpsToPatch.emplace(jump.backwardJumpIdx,
                                 mgenc.OffsetOfNextInstruction());
    }
}

void VMMethod::AdaptAfterOuterInlined(
    uint8_t removedCtxLevel, MethodGenerationContext& mgencWithInlined) {
    uint8_t* bytecodes = GetBytecodes();
    size_t i = 0;
    size_t const numBytecodes = GetNumberOfBytecodes();

    while (i < numBytecodes) {
        uint8_t const bytecode = bytecodes[i];
        size_t const bcLength = Bytecode::GetBytecodeLength(bytecode);

        switch (bytecode) {
            case BC_DUP:
            case BC_DUP_SECOND:
            case BC_PUSH_CONSTANT:
            case BC_PUSH_0:
            case BC_PUSH_1:
            case BC_PUSH_SMALL_INT:
            case BC_PUSH_NIL:
            case BC_PUSH_GLOBAL:  // BC_PUSH_GLOBAL doesn't encode context
            case BC_PUSH_FIELD:
            case BC_POP_FIELD:
            case BC_POP:
            case BC_SEND:
            case BC_SEND_1:
            case BC_SEND_2:
            case BC_SEND_3:
            case BC_SEND_EQEQ:
            case BC_SUPER_SEND:
            case BC_RETURN_LOCAL:
            case BC_RETURN_NON_LOCAL:
            case BC_INC:
            case BC_DEC:
            case BC_JUMP:
            case BC_INC_FIELD_PUSH:  // just field index, implicit/dynamic self
                                     // lookup in SOM++
            case BC_INC_FIELD:       // just field index, implicit/dynamic self
                                     // lookup in SOM++
            case BC_JUMP_ON_TRUE_TOP_NIL:
            case BC_JUMP_ON_TRUE_POP:
            case BC_JUMP_ON_FALSE_TOP_NIL:
            case BC_JUMP_ON_FALSE_POP:
            case BC_JUMP_ON_NOT_NIL_TOP_TOP:
            case BC_JUMP_ON_NIL_TOP_TOP:
            case BC_JUMP_ON_NOT_NIL_POP:
            case BC_JUMP_ON_NIL_POP:
            case BC_JUMP_BACKWARD:
            case BC_JUMP2:
            case BC_JUMP2_ON_TRUE_TOP_NIL:
            case BC_JUMP2_ON_TRUE_POP:
            case BC_JUMP2_ON_FALSE_TOP_NIL:
            case BC_JUMP2_ON_FALSE_POP:
            case BC_JUMP2_ON_NOT_NIL_TOP_TOP:
            case BC_JUMP2_ON_NIL_TOP_TOP:
            case BC_JUMP2_ON_NOT_NIL_POP:
            case BC_JUMP2_ON_NIL_POP:
            case BC_JUMP2_BACKWARD: {
                // these bytecodes do not use context and don't need to be
                // adapted
                break;
            }

            case BC_PUSH_ARGUMENT:
            case BC_POP_ARGUMENT: {
                uint8_t const idx = bytecodes[i + 1];
                uint8_t const ctxLevel = bytecodes[i + 2];

                uint8_t const newContextLevel =
                    ctxLevel > removedCtxLevel ? ctxLevel - 1 : ctxLevel;

                const Variable* oldArg =
                    lexicalScope->GetArgument(idx, ctxLevel);
                const Variable* newVar =
                    mgencWithInlined.GetInlinedVariable(oldArg);

                if (newVar->IsArgument()) {
                    if (ctxLevel > removedCtxLevel) {
                        bytecodes[i + 2] = newContextLevel;
                    }
                    assert(newVar->GetIndex() == idx);
                } else {
                    bytecodes[i] = bytecode == BC_PUSH_ARGUMENT ? BC_PUSH_LOCAL
                                                                : BC_POP_LOCAL;
                    bytecodes[i + 1] = newVar->GetIndex();
                    if (ctxLevel > removedCtxLevel) {
                        bytecodes[i + 2] = newContextLevel;
                    }
                }
                break;
            }

            case BC_PUSH_BLOCK: {
                auto* blockMethod = static_cast<VMMethod*>(GetConstant(i));
                blockMethod->AdaptAfterOuterInlined(removedCtxLevel + 1,
                                                    mgencWithInlined);
                break;
            }

            case BC_PUSH_LOCAL:
            case BC_POP_LOCAL: {
                uint8_t const ctxLevel = bytecodes[i + 2];
                if (ctxLevel == removedCtxLevel) {
                    uint8_t const idx = bytecodes[i + 1];

                    // locals have been inlined into the outer context already
                    // so, we need to look up the right one and fix up the index
                    // at this point, the lexical scope has not been changed
                    // so, we should still be able to find the right one
                    const auto* oldVar = lexicalScope->GetLocal(idx, ctxLevel);
                    uint8_t const newIdx =
                        mgencWithInlined.GetInlinedLocalIdx(oldVar);
                    bytecodes[i + 1] = newIdx;
                } else if (ctxLevel > removedCtxLevel) {
                    bytecodes[i + 2] = ctxLevel - 1;
                }
                break;
            }

            case BC_PUSH_SELF:
            case BC_PUSH_ARG_1:
            case BC_PUSH_ARG_2:
            case BC_PUSH_LOCAL_0:
            case BC_PUSH_LOCAL_1:
            case BC_PUSH_LOCAL_2:
            case BC_PUSH_FIELD_0:
            case BC_PUSH_FIELD_1:
            case BC_POP_LOCAL_0:
            case BC_POP_LOCAL_1:
            case BC_POP_LOCAL_2:
            case BC_POP_FIELD_0:
            case BC_POP_FIELD_1: {
                break;
            }

            case BC_HALT:
            case BC_RETURN_SELF:
            case BC_RETURN_FIELD_0:
            case BC_RETURN_FIELD_1:
            case BC_RETURN_FIELD_2: {
                char msg[120];
                (void)snprintf(
                    msg, 120,
                    "AdaptAfterOuterInlined: Found %s bytecode, but it's not "
                    "expected in a block method",
                    Bytecode::GetBytecodeName(bytecode));
                ErrorExit(msg);
            }

            default: {
                char msg[120];
                (void)snprintf(
                    msg, 120,
                    "Found %s bytecode, but AdaptAfterOuterInlined does "
                    "not yet "
                    "support it.",
                    Bytecode::GetBytecodeName(bytecode));
                ErrorExit(msg);
            }
        }

        i += bcLength;
    }

    if (removedCtxLevel == 1) {
        lexicalScope->DropInlinedScope();
    }
}
#endif

#ifndef FRUCTUS
bool operator<(const Jump& a, const Jump& b) {
    return a.originalJumpTargetIdx > b.originalJumpTargetIdx;
}
#endif

#ifndef FRUCTUS
bool operator<(const BackJump& a, const BackJump& b) {
    return a.loopBeginIdx > b.loopBeginIdx;
}

bool operator<(const BackJumpPatch& a, const BackJumpPatch& b) {
    return a.backwardsJumpIdx > b.backwardsJumpIdx;
}
#endif

#ifndef FRUCTUS
void VMMethod::MergeScopeInto(MethodGenerationContext& mgenc) {
    assert(lexicalScope != nullptr);
    mgenc.MergeIntoScope(*lexicalScope);
}
#endif

#ifndef FRUCTUS
std::vector<uint8_t> VMMethod::ResizeWords(
    size_t wordSize, const std::function<void(void*, uint8_t*)>& writeWord,
    bool unaryNumberOfArguments,
    const std::function<uint8_t(size_t)>& newBytecode,
    bool oneWordSends) const {
    const uint8_t* bytecodes = GetBytecodes();
    // the bytecode at i becomes
    auto newBc = [&](size_t i) -> uint8_t {
        uint8_t const bc = newBytecode ? newBytecode(i) : bytecodes[i];
        assert(bc == bytecodes[i] || IsSendBytecode(bytecodes[i]));
        return bc;
    };
    auto numberOfWords = [&](uint8_t bc) -> uint8_t {
        return oneWordSends && IsSendBytecode(bc) ? 1 : GetNumberOfWords(bc);
    };
    // the bytes before the words
    auto numberOfBytes = [](uint8_t bc) -> size_t {
        return Bytecode::GetBytecodeLength(bc) -
               GetNumberOfWords(bc) * sizeof(void*);
    };
    // where each bytecode goes
    std::vector<size_t> newIndex(bcLength + 1);
    size_t n = 0;
    for (size_t i = 0; i < bcLength;
         i += Bytecode::GetBytecodeLength(bytecodes[i])) {
        uint8_t const bc = newBc(i);
        newIndex[i] = n;
        n += numberOfBytes(bc) + numberOfWords(bc) * wordSize +
             (unaryNumberOfArguments && bc == BC_SEND_1 ? 1 : 0);
    }
    newIndex[bcLength] = n;

    std::vector<uint8_t> result(n);
    for (size_t i = 0; i < bcLength;
         i += Bytecode::GetBytecodeLength(bytecodes[i])) {
        uint8_t const bc = newBc(i);
        uint8_t* out = &result[newIndex[i]];
        uint8_t const numWords = numberOfWords(bc);
        size_t const numBytes = numberOfBytes(bc);
        if (bc == bytecodes[i]) {
            memcpy(out, &bytecodes[i], numBytes);
        } else if (bc == BC_NOT_UNDERSTOOD) {
            out[SendSite::numberOfArguments] =
                SendSite::GetNumberOfArguments(&bytecodes[i]);
        }
        out[0] = bc;
        size_t numOutBytes = numBytes;
        if (unaryNumberOfArguments && bc == BC_SEND_1) {
            out[numOutBytes++] = 1;
        }
        // the word of a static send or a NOT_UNDERSTOOD is the target of the
        // send it was, or its selector
        size_t const words =
            bc == bytecodes[i] ? numBytes : SendSite::Target(bytecodes[i]);
        for (uint8_t w = 0; w < numWords; w++) {
            writeWord(LoadWord(&bytecodes[i + words + w * sizeof(void*)]),
                      out + numOutBytes + w * wordSize);
        }
        if (!IsJumpBytecode(bc)) {
            continue;
        }

        // the offset is from the jump, and the jump only gets shorter, so
        // it takes the short form if the offset fits in a byte, as
        // MethodGenerationContext does
        bool const isLong = bc >= FIRST_DOUBLE_BYTE_JUMP_BYTECODE;
        bool const isBackward =
            bc == BC_JUMP_BACKWARD || bc == BC_JUMP2_BACKWARD;
        size_t const offset = isLong
                                  ? ComputeOffset(bytecodes[i + 1],
                                                  bytecodes[i + 2])
                                  : bytecodes[i + 1];
        size_t const target = isBackward ? i - offset : i + offset;
        size_t const newOffset = isBackward
                                     ? newIndex[i] - newIndex[target]
                                     : newIndex[target] - newIndex[i];
        assert(newOffset <= offset);
        uint8_t const shortBc =
            isLong ? bc - NUM_SINGLE_BYTE_JUMP_BYTECODES : bc;
        out[0] = newOffset <= 0xFF ? shortBc
                                   : shortBc + NUM_SINGLE_BYTE_JUMP_BYTECODES;
        out[1] = newOffset & 0xFFU;
        out[2] = newOffset >> 8U;
    }
    return result;
}

/**
 * The hash of the bytecodes with each word replaced by a byte, which is the
 * index of its first occurrence in the method, so that it doesn't depend on
 * where the objects are. The sends are unlinked when the primitives are
 * loaded, which is when this is used. A unary send has its number of
 * arguments, which it used to, to keep the hashes the primitives have.
 */
size_t VMMethod::GetBytecodeHash() const {
    if (bytecodeHash != 0) {
        return bytecodeHash;
    }
    std::vector<void*> words;
    std::vector<uint8_t> const bcs =
        ResizeWords(1, [&words](void* word, uint8_t* out) {
            size_t index = 0;
            while (index < words.size() && words[index] != word) {
                index++;
            }
            if (index == words.size()) {
                words.push_back(word);
            }
            *out = index;
        },
        true);
    return murmur3_32(bcs.data(), bcs.size(), 0x00000000);
}
#endif

#ifndef FRUCTUS
VMMethod* VMMethod::MakeStackRelative() {
    const uint8_t* bytecodes = GetBytecodes();
    std::vector<int> const depths = ComputeStackDepths(bytecodes, bcLength);
    size_t const numArgs = GetNumberOfArguments();

    // The frame slot, counting the arguments and then the locals, of the
    // argument or local in this context that the bytecode at i accesses, or
    // -1.
    auto slot = [&](size_t i) -> int {
        switch (bytecodes[i]) {
            case BC_PUSH_SELF:
                return 0;
            case BC_PUSH_ARG_1:
                return 1;
            case BC_PUSH_ARG_2:
                return 2;
            case BC_PUSH_LOCAL_0:
            case BC_POP_LOCAL_0:
                return (int)numArgs;
            case BC_PUSH_LOCAL_1:
            case BC_POP_LOCAL_1:
                return (int)numArgs + 1;
            case BC_PUSH_LOCAL_2:
            case BC_POP_LOCAL_2:
                return (int)numArgs + 2;
            case BC_PUSH_ARGUMENT:
            case BC_POP_ARGUMENT:
                return bytecodes[i + 2] == 0 ? bytecodes[i + 1] : -1;
            case BC_PUSH_LOCAL:
            case BC_POP_LOCAL:
                return bytecodes[i + 2] == 0 ? (int)numArgs + bytecodes[i + 1]
                                             : -1;
            default:
                return -1;
        }
    };
    auto isPop = [](uint8_t bc) {
        return bc == BC_POP_LOCAL || bc == BC_POP_LOCAL_0 ||
               bc == BC_POP_LOCAL_1 || bc == BC_POP_LOCAL_2 ||
               bc == BC_POP_ARGUMENT;
    };
    auto newLength = [&](size_t i) -> size_t {
        return slot(i) == -1 ? Bytecode::GetBytecodeLength(bytecodes[i])
                             : Bytecode::GetBytecodeLength(BC_PUSH_STACK);
    };

    // where each bytecode goes
    std::vector<size_t> newIndex(bcLength + 1);
    size_t n = 0;
    for (size_t i = 0; i < bcLength;
         i += Bytecode::GetBytecodeLength(bytecodes[i])) {
        newIndex[i] = n;
        n += newLength(i);
    }
    newIndex[bcLength] = n;

    std::vector<uint8_t> result(n);
    std::vector<uint16_t> newLines(n);
    for (size_t i = 0; i < bcLength;
         i += Bytecode::GetBytecodeLength(bytecodes[i])) {
        uint8_t const bc = bytecodes[i];
        uint8_t* out = &result[newIndex[i]];
        for (size_t j = newIndex[i]; j < newIndex[i] + newLength(i); j++) {
            newLines[j] = GetLine(i);
        }

        int const s = slot(i);
        if (s != -1) {
            // The stack starts right after the last local, and an
            // unreachable bytecode has no depth, and doesn't matter.
            int const depth = depths[i] == -1 ? 0 : depths[i];
            int const offset = (int)(numArgs + numberOfLocals) - 1 + depth - s;
            assert(offset >= 0);
            if (offset > 0xFF) {
                ErrorExit("A method has too many locals and stack elements");
            }
            out[0] = isPop(bc) ? BC_POP_STACK : BC_PUSH_STACK;
            out[1] = offset;
            continue;
        }

        memcpy(out, &bytecodes[i], Bytecode::GetBytecodeLength(bc));

        if (bc == BC_PUSH_BLOCK) {
            auto* block = (VMInvokable*)GetConstant(i);
            if (IsVMMethod((vm_oop_t)block)) {
                block = static_cast<VMMethod*>(block)->MakeStackRelative();
            }
            StoreWord(out + 1, block);
            continue;
        }

        if (!IsJumpBytecode(bc)) {
            continue;
        }
        bool const isLong = bc >= FIRST_DOUBLE_BYTE_JUMP_BYTECODE;
        bool const isBackward =
            bc == BC_JUMP_BACKWARD || bc == BC_JUMP2_BACKWARD;
        size_t const offset =
            isLong ? ComputeOffset(bytecodes[i + 1], bytecodes[i + 2])
                   : bytecodes[i + 1];
        size_t const target = isBackward ? i - offset : i + offset;
        size_t const newOffset = isBackward ? newIndex[i] - newIndex[target]
                                            : newIndex[target] - newIndex[i];
        if (newOffset > 0xFFFF) {
            ErrorExit("A jump is too long");
        }
        uint8_t const shortBc =
            isLong ? bc - NUM_SINGLE_BYTE_JUMP_BYTECODES : bc;
        out[0] = newOffset <= 0xFF ? shortBc
                                   : shortBc + NUM_SINGLE_BYTE_JUMP_BYTECODES;
        out[1] = newOffset & 0xFFU;
        out[2] = newOffset >> 8U;
    }

    std::vector<BackJump> noLoops;
    VMMethod* lowered = Universe::NewMethod(GetSignature(), n, numberOfLocals,
                                            maximumNumberOfStackElements,
                                            lexicalScope, noLoops);
    memcpy(lowered->GetBytecodes(), result.data(), n);
    lowered->SetLines(newLines);
    lowered->bytecodeHash = GetBytecodeHash();
    // the lowered method owns them now
    lexicalScope = nullptr;
    return lowered;
}
#endif
