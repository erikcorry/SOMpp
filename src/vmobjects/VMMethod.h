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

#ifndef FRUCTUS
#include <functional>
#include <queue>
#include <vector>
#endif

#ifndef FRUCTUS
#include "../compiler/LexicalScope.h"
#endif
#include "../interpreter/bytecodes.h"
#include "../vm/Globals.h"
#include "../vm/Print.h"
#include "VMInteger.h"
#include "VMInvokable.h"

class MethodGenerationContext;
class Interpreter;
class Parser;

#ifndef FRUCTUS
class Jump {
public:
    Jump(size_t originalJumpTargetIdx, uint8_t jumpBc, size_t idx)
        : originalJumpTargetIdx(originalJumpTargetIdx), jumpBc(jumpBc),
          idx(idx) {}

    //    Jump(Jump&& jmp) = default;
    //    Jump(const Jump& jmp) = default;

    size_t originalJumpTargetIdx;  // order by
    uint8_t jumpBc;
    size_t idx;
};
#endif

#ifndef FRUCTUS
bool operator<(const Jump& a, const Jump& b);
#endif

#ifndef FRUCTUS
class BackJump {
public:
    BackJump(size_t loopBeginIdx, size_t backwardJumpIdx)
        : loopBeginIdx(loopBeginIdx), backwardJumpIdx(backwardJumpIdx) {}

    BackJump(const BackJump& jmp) = default;

    BackJump() : loopBeginIdx(-1), backwardJumpIdx(-1) {}

    size_t loopBeginIdx;  // order by
    size_t backwardJumpIdx;

    [[nodiscard]] bool IsValid() const { return loopBeginIdx != -1; }
};

bool operator<(const BackJump& a, const BackJump& b);

class BackJumpPatch {
public:
    BackJumpPatch(size_t backwardsJumpIdx, size_t loopBeginIdx)
        : backwardsJumpIdx(backwardsJumpIdx), loopBeginIdx(loopBeginIdx) {}

    BackJumpPatch(const BackJumpPatch& patch) = default;

    size_t backwardsJumpIdx;  // order by
    size_t loopBeginIdx;
};

bool operator<(const BackJumpPatch& a, const BackJumpPatch& b);
#endif

class VMMethod : public VMInvokable {
    friend class Interpreter;
    friend class Disassembler;
    friend class FructusImage;

public:
    typedef GCMethod Stored;

#ifdef FRUCTUS
    VMMethod(VMSymbol* signature, size_t bcCount, size_t numLocals,
             size_t maxStackDepth);
#else
    VMMethod(VMSymbol* signature, size_t bcCount, size_t numLocals,
             size_t maxStackDepth, LexicalScope* lexicalScope,
             BackJump* inlinedLoops);

    ~VMMethod() override { delete lexicalScope; }
#endif

    [[nodiscard]] inline size_t GetNumberOfLocals() const {
        return numberOfLocals;
    }

    [[nodiscard]] VMClass* GetClass() const override {
        return load_ptr(methodClass);
    }

#ifdef FRUCTUS
    /**
     * The bytecodes come right after numberOfArguments, an odd offset, since
     * the bytecodes don't need aligning. ImageFructus.cpp checks it.
     */
    static constexpr size_t headerSize = 13;

    [[nodiscard]] inline size_t GetObjectSize() const override {
        return PADDED_SIZE(headerSize + bcLength);
    }
#else
    [[nodiscard]] inline size_t GetObjectSize() const override {
        return sizeof(VMMethod) + PADDED_SIZE(bcLength);
    }
#endif

    [[nodiscard]] size_t GetMaximumNumberOfStackElements() const {
        return maximumNumberOfStackElements;
    }

    [[nodiscard]] inline uint8_t GetNumberOfArguments() const final {
        return numberOfArguments;
    }

    [[nodiscard]] size_t GetNumberOfBytecodes() const { return bcLength; }

#ifndef FRUCTUS
    [[nodiscard]] size_t GetBytecodeHash() const;

    /**
     * The bytecodes with each word `wordSize` long, and the jumps adjusted.
     * `writeWord(word, out)` writes each of them.
     */
    [[nodiscard]] std::vector<uint8_t> ResizeWords(
        size_t wordSize,
        const std::function<void(void*, uint8_t*)>& writeWord) const;
#endif

    /** The selector of the send at `bytecodeIndex`, see SendSite.h. */
    [[nodiscard]] VMSymbol* GetSendSelector(size_t bytecodeIndex) const;

    void SetHolder(VMClass* hld) override;
    void SetHolderAll(VMClass* hld) const;

    /**
     * The constant of the PUSH_CONSTANT, the method of the PUSH_BLOCK, or
     * the name of the PUSH_GLOBAL at `bytecodeIndex`.
     */
    [[nodiscard]] inline vm_oop_t GetConstant(size_t bytecodeIndex) const {
        return (vm_oop_t)GetWordOperand(&GetBytecodes()[bytecodeIndex]);
    }

    [[nodiscard]] inline uint8_t GetBytecode(size_t indx) const {
        return GetBytecodes()[indx];
    }

    inline void SetBytecode(size_t indx, uint8_t val) {
        GetBytecodes()[indx] = val;
    }

#ifdef UNSAFE_FRAME_OPTIMIZATION
    void SetCachedFrame(VMFrame* frame);
    GCFrame* GetCachedFrame() const;
#endif

    void WalkObjects(walk_heap_fn /*unused*/) override;

    [[nodiscard]] VMMethod* CloneForMovingGC() const override;

    VMFrame* Invoke(VMFrame* frame) override;
    VMFrame* Invoke1(VMFrame* frame) override;

    [[nodiscard]] bool IsMarkedInvalid() const override {
        return signature == (GCSymbol*)INVALID_GC_POINTER;
    }

    DECLARE_AS_DEBUG_STRING(override)

#ifndef FRUCTUS
    void InlineInto(MethodGenerationContext& mgenc, const Parser& parser,
                    bool mergeScope = true) final;

    void AdaptAfterOuterInlined(
        uint8_t removedCtxLevel,
        MethodGenerationContext& mgencWithInlined) final;

    void MergeScopeInto(MethodGenerationContext& mgenc) override;

    const Variable* GetArgument(size_t index, size_t contextLevel) override {
        return lexicalScope->GetArgument(index, contextLevel);
    }
#endif

#ifndef FRUCTUS
    void Dump(const char* indent, bool printObjects) override;
#endif

    /** The bytecodes come right after the method. */
    [[nodiscard]] inline uint8_t* GetBytecodes() const {
#ifdef FRUCTUS
        return (uint8_t*)this + headerSize;
#else
        return (uint8_t*)(this + 1);
#endif
    }

private:
#ifndef FRUCTUS
    void inlineInto(MethodGenerationContext& mgenc, const Parser& parser);
    std::priority_queue<BackJump> createBackJumpHeap();

    static void prepareBackJumpToCurrentAddress(
        std::priority_queue<BackJump>& backJumps,
        std::priority_queue<BackJumpPatch>& backJumpsToPatch, size_t i,
        MethodGenerationContext& mgenc);

    static void patchJumpToCurrentAddress(size_t i,
                                          std::priority_queue<Jump>& jumps,
                                          MethodGenerationContext& mgenc);
#endif

    make_testable(public);

#ifdef FRUCTUS
    // bytes, and the bytecodes follow at headerSize, with no padding
    const size_t bcLength;
    const uint8_t numberOfLocals;
    const uint8_t maximumNumberOfStackElements;
    const uint8_t numberOfArguments;
#else
    const size_t numberOfLocals;
    const size_t maximumNumberOfStackElements;
    const size_t bcLength;
    const uint8_t numberOfArguments;
#endif

    make_testable(private);
#ifndef FRUCTUS
    LexicalScope* lexicalScope;
    BackJump* inlinedLoops;
#endif

#ifdef UNSAFE_FRAME_OPTIMIZATION
    GCFrame* cachedFrame;
#endif

#ifdef BYTECODE_HEATMAP
    uint64_t* heatmap;
#endif
};
