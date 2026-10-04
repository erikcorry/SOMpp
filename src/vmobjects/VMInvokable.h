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

#include "ObjectFormats.h"
#include "VMObject.h"
#include "VMSymbol.h"

class MethodGenerationContext;
class Variable;
class VMFrame;
class Parser;

class VMInvokable : public AbstractVMObject {
    friend class FructusImage;

public:
    typedef GCInvokable Stored;

#ifdef FRUCTUS
    explicit VMInvokable(VMSymbol* sig)
        : signature(store_with_separate_barrier(sig)) {}

    [[nodiscard]] som_int_t GetHash() const override {
        return AddressHash(this);
    }
#else
    explicit VMInvokable(VMSymbol* sig)
        : hash((intptr_t)this), signature(store_with_separate_barrier(sig)) {}

    [[nodiscard]] som_int_t GetHash() const override { return hash; }
#endif

    virtual VMFrame* Invoke(VMFrame*) = 0;
    virtual VMFrame* Invoke1(VMFrame*) = 0;
#ifndef FRUCTUS
    virtual void InlineInto(MethodGenerationContext& mgenc,
                            const Parser& parser, bool mergeScope = true) = 0;
    virtual void MergeScopeInto(
        MethodGenerationContext& /*unused*/) { /* NOOP for everything but
                                                  VMMethods */
    }
    virtual void AdaptAfterOuterInlined(
        uint8_t removedCtxLevel,
        MethodGenerationContext&
            mgencWithInlined) { /* NOOP for everything but VMMethods */ }
#endif
#ifndef FRUCTUS
    virtual const Variable* GetArgument(size_t /*unused*/, size_t /*unused*/);
#endif

    [[nodiscard]] virtual uint8_t GetNumberOfArguments() const = 0;

    [[nodiscard]] virtual bool IsPrimitive() const;

    [[nodiscard]] inline VMSymbol* GetSignature() const {
        return load_ptr(signature);
    }

#ifndef FRUCTUS
    // fructus links the super sends in the image, so it needs no holders
    [[nodiscard]] VMClass* GetHolder() const { return load_ptr(holder); }

    virtual void SetHolder(VMClass* hld);
#endif

    void WalkObjects(walk_heap_fn /*unused*/) override;

    void MarkObjectAsInvalid() override {
        signature = (GCSymbol*)INVALID_GC_POINTER;
#ifndef FRUCTUS
        holder = (GCClass*)INVALID_GC_POINTER;
#endif
    }

#ifndef FRUCTUS
    virtual void Dump(const char* indent, bool printObjects) = 0;
#endif

protected:
    make_testable(public);

#ifndef FRUCTUS
    som_int_t hash;
#endif

    GCSymbol* signature;
#ifndef FRUCTUS
    GCClass* holder{nullptr};
#endif
};
