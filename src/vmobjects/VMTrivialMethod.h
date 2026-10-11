#pragma once

#ifndef FRUCTUS
#include "../compiler/MethodGenerationContext.h"
#endif
#include "../vm/Globals.h"
#include "ObjectFormats.h"
#include "Signature.h"
#include "VMInvokable.h"
#include "VMSymbol.h"

// on fructus, trivial methods are not created by the compiler, and don't keep
// their arguments for inlining
#ifdef FRUCTUS
  #define TRIVIAL_ARGUMENTS_PARAM
  #define TRIVIAL_ARGUMENTS
#else
  #define TRIVIAL_ARGUMENTS_PARAM vector<Variable>&arguments,
  #define TRIVIAL_ARGUMENTS , arguments
#endif

class VMTrivialMethod : public VMInvokable {
public:
    typedef GCTrivialMethod Stored;

#ifdef FRUCTUS
    explicit VMTrivialMethod(VMSymbol* sig) : VMInvokable(sig) {}
#else
    VMTrivialMethod(VMSymbol* sig, vector<Variable>& arguments)
        : VMInvokable(sig), arguments(arguments) {}
#endif

    [[nodiscard]] VMClass* GetClass() const final {
        return load_ptr(methodClass);
    }

    [[nodiscard]] bool IsPrimitive() const final { return false; };

#ifndef FRUCTUS
    void MergeScopeInto(MethodGenerationContext& mgenc) final {
        if (!arguments.empty()) {
            mgenc.InlineAsLocals(arguments);
        }
    }

    const Variable* GetArgument(size_t index, size_t contextLevel) final {
        assert(contextLevel == 0);
        if (contextLevel > 0) {
            return nullptr;
        }
        return &arguments.at(index);
    }
#endif

#ifndef FRUCTUS
    void Dump(const char* indent, bool printObjects) override;
#endif

#ifndef FRUCTUS
private:
    vector<Variable> arguments;
#endif
};

#ifndef FRUCTUS
VMTrivialMethod* MakeLiteralReturn(VMSymbol* sig, vector<Variable>& arguments,
                                   vm_oop_t literal);
VMTrivialMethod* MakeGlobalReturn(VMSymbol* sig, vector<Variable>& arguments,
                                  VMSymbol* globalName);
VMTrivialMethod* MakeGetter(VMSymbol* sig, vector<Variable>& arguments,
                            size_t fieldIndex);
VMTrivialMethod* MakeSetter(VMSymbol* sig, vector<Variable>& arguments,
                            size_t fieldIndex, size_t argIndex);
#endif

class VMLiteralReturn : public VMTrivialMethod {
    friend class FructusImage;

public:
    typedef GCLiteralReturn Stored;

    // the image has them on fructus
#ifndef FRUCTUS
    VMLiteralReturn(VMSymbol* sig, TRIVIAL_ARGUMENTS_PARAM vm_oop_t literal)
        : VMTrivialMethod(sig TRIVIAL_ARGUMENTS),
          literal(store_with_separate_barrier(literal)),
          numberOfArguments(Signature::GetNumberOfArguments(sig)) {}
#endif

    [[nodiscard]] inline size_t GetObjectSize() const override {
        return sizeof(VMLiteralReturn);
    }

    VMFrame* Invoke(VMFrame* /*frame*/) override;
    VMFrame* Invoke1(VMFrame* /*frame*/) override;

#ifndef FRUCTUS
    void InlineInto(MethodGenerationContext& mgenc, const Parser& parser,
                    bool mergeScope = true) final;
#endif

    [[nodiscard]] AbstractVMObject* CloneForMovingGC() const final;

    void MarkObjectAsInvalid() final {
        VMTrivialMethod::MarkObjectAsInvalid();
        literal = INVALID_GC_POINTER;
    }

    void WalkObjects(walk_heap_fn /*walk*/) override;

    [[nodiscard]] bool IsMarkedInvalid() const final {
        return literal == INVALID_GC_POINTER;
    }

    DECLARE_AS_DEBUG_STRING(final)

    [[nodiscard]] inline uint8_t GetNumberOfArguments() const final {
        return numberOfArguments;
    }

private:
    gc_oop_t literal;
    uint8_t numberOfArguments;
};

class VMGlobalReturn : public VMTrivialMethod {
    friend class FructusImage;

public:
    typedef GCGlobalReturn Stored;

    // the image has them on fructus
#ifndef FRUCTUS
    VMGlobalReturn(VMSymbol* sig, TRIVIAL_ARGUMENTS_PARAM VMSymbol* globalName)
        : VMTrivialMethod(sig TRIVIAL_ARGUMENTS),
          globalName(store_with_separate_barrier(globalName)),
          numberOfArguments(Signature::GetNumberOfArguments(sig)) {}
#endif

    [[nodiscard]] inline size_t GetObjectSize() const override {
        return sizeof(VMGlobalReturn);
    }

    VMFrame* Invoke(VMFrame* /*frame*/) override;
    VMFrame* Invoke1(VMFrame* /*frame*/) override;

#ifndef FRUCTUS
    void InlineInto(MethodGenerationContext& mgenc, const Parser& parser,
                    bool mergeScope = true) final;
#endif

    [[nodiscard]] AbstractVMObject* CloneForMovingGC() const final;

    void MarkObjectAsInvalid() final {
        VMTrivialMethod::MarkObjectAsInvalid();
        globalName = (GCSymbol*)INVALID_GC_POINTER;
    }

    void WalkObjects(walk_heap_fn /*walk*/) override;

    [[nodiscard]] bool IsMarkedInvalid() const final {
        return globalName == (GCSymbol*)INVALID_GC_POINTER;
    }

    DECLARE_AS_DEBUG_STRING(final)

    [[nodiscard]] inline uint8_t GetNumberOfArguments() const final {
        return numberOfArguments;
    }

private:
    void sendUnknownGlobal(VMFrame* frame);

    GCSymbol* globalName;
    uint8_t numberOfArguments;
};

class VMGetter : public VMTrivialMethod {
    friend class FructusImage;

public:
    typedef GCGetter Stored;

    // the image has them on fructus
#ifndef FRUCTUS
    VMGetter(VMSymbol* sig, TRIVIAL_ARGUMENTS_PARAM size_t fieldIndex)
        : VMTrivialMethod(sig TRIVIAL_ARGUMENTS), fieldIndex(fieldIndex),
          numberOfArguments(Signature::GetNumberOfArguments(sig)) {}
#endif

    [[nodiscard]] inline size_t GetObjectSize() const override {
        return sizeof(VMGetter);
    }

    VMFrame* Invoke(VMFrame* /*frame*/) override;
    VMFrame* Invoke1(VMFrame* /*frame*/) override;

#ifndef FRUCTUS
    void InlineInto(MethodGenerationContext& mgenc, const Parser& parser,
                    bool mergeScope = true) final;
#endif

    [[nodiscard]] AbstractVMObject* CloneForMovingGC() const final;

    void MarkObjectAsInvalid() final { VMTrivialMethod::MarkObjectAsInvalid(); }

    [[nodiscard]] bool IsMarkedInvalid() const final {
        return isInvokableMarkedInvalid();
    }

    void WalkObjects(walk_heap_fn /*walk*/) override;

    DECLARE_AS_DEBUG_STRING(final)

    [[nodiscard]] inline uint8_t GetNumberOfArguments() const final {
        return numberOfArguments;
    }

private:
    size_t fieldIndex;
    uint8_t numberOfArguments;
};

class VMSetter : public VMTrivialMethod {
    friend class FructusImage;

public:
    typedef GCSetter Stored;

    // the image has them on fructus
#ifndef FRUCTUS
    VMSetter(VMSymbol* sig, TRIVIAL_ARGUMENTS_PARAM size_t fieldIndex,
             size_t argIndex)
        : VMTrivialMethod(sig TRIVIAL_ARGUMENTS), fieldIndex(fieldIndex),
          argIndex(argIndex),
          numberOfArguments(Signature::GetNumberOfArguments(sig)) {}
#endif

    [[nodiscard]] inline size_t GetObjectSize() const override {
        return sizeof(VMSetter);
    }

    VMFrame* Invoke(VMFrame* /*frame*/) override;
    VMFrame* Invoke1(VMFrame* /*unused*/) override;

#ifndef FRUCTUS
    void InlineInto(MethodGenerationContext& mgenc, const Parser& parser,
                    bool mergeScope = true) final;
#endif

    [[nodiscard]] AbstractVMObject* CloneForMovingGC() const final;

    void MarkObjectAsInvalid() final { VMTrivialMethod::MarkObjectAsInvalid(); }

    [[nodiscard]] bool IsMarkedInvalid() const final {
        return isInvokableMarkedInvalid();
    }

    void WalkObjects(walk_heap_fn /*walk*/) override;

    DECLARE_AS_DEBUG_STRING(final)

    [[nodiscard]] inline uint8_t GetNumberOfArguments() const final {
        return numberOfArguments;
    }

private:
    make_testable(public);

    size_t fieldIndex;
    size_t argIndex;
    uint8_t numberOfArguments;
};
