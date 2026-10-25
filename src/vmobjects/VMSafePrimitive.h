#pragma once

#include "../primitivesCore/Primitives.h"
#include "Signature.h"
#include "VMInvokable.h"

class VMSafePrimitive : public VMInvokable {
public:
    typedef GCSafePrimitive Stored;

    explicit VMSafePrimitive(VMSymbol* sig) : VMInvokable(sig) {}

    [[nodiscard]] VMClass* GetClass() const final {
        return load_ptr(primitiveClass);
    }

    [[nodiscard]] bool IsPrimitive() const final { return true; };

#ifndef FRUCTUS
    void InlineInto(MethodGenerationContext& mgenc, const Parser& parser,
                    bool mergeScope = true) final;
#endif

    static VMInvokable* GetSafeUnary(VMSymbol* sig, UnaryPrim prim);
    static VMInvokable* GetSafeBinary(VMSymbol* sig, BinaryPrim prim);
    static VMInvokable* GetSafeTernary(VMSymbol* sig, TernaryPrim prim);

#ifndef FRUCTUS
    /**
     * For a routine that fails with an error: its primitive then sends
     * #error: with `message` to its receiver, in place of its own send.
     * Answers the message, as the routine's result, or nullptr if it can't
     * be made, which makes the send run again after a collection.
     */
    static vm_oop_t SendError(const char* message);
#endif

    DECLARE_AS_DEBUG_STRING(final)

#ifndef FRUCTUS
    void Dump(const char* indent, bool printObjects) override;
#endif
};

class VMSafeUnaryPrimitive : public VMSafePrimitive {
    friend class FructusImage;

public:
    typedef GCSafeUnaryPrimitive Stored;

    VMSafeUnaryPrimitive(VMSymbol* sig, UnaryPrim prim)
        : VMSafePrimitive(sig), prim(prim) {}

    [[nodiscard]] inline size_t GetObjectSize() const override {
        return sizeof(VMSafeUnaryPrimitive);
    }

    gc_oop_t* Invoke(VMFrame* /*frame*/, gc_oop_t* /*sp*/) override;
    gc_oop_t* Invoke1(VMFrame* /*frame*/, gc_oop_t* /*sp*/) override;

    [[nodiscard]] AbstractVMObject* CloneForMovingGC() const final;

    void MarkObjectAsInvalid() final {
        VMSafePrimitive::MarkObjectAsInvalid();
        prim.MarkObjectAsInvalid();
    }

    [[nodiscard]] bool IsMarkedInvalid() const final { return !prim.IsValid(); }

    // the receiver included
    [[nodiscard]] inline uint8_t GetNumberOfArguments() const final {
        return 1;
    }

private:
    UnaryPrim prim;
};

class VMSafeBinaryPrimitive : public VMSafePrimitive {
    friend class FructusImage;

public:
    typedef GCSafeBinaryPrimitive Stored;

    VMSafeBinaryPrimitive(VMSymbol* sig, BinaryPrim prim)
        : VMSafePrimitive(sig), prim(prim) {}

    [[nodiscard]] inline size_t GetObjectSize() const override {
        return sizeof(VMSafeBinaryPrimitive);
    }

    gc_oop_t* Invoke(VMFrame* /*frame*/, gc_oop_t* /*sp*/) override;
    gc_oop_t* Invoke1(VMFrame* /*unused*/, gc_oop_t* /*sp*/) override;

    [[nodiscard]] AbstractVMObject* CloneForMovingGC() const final;

    void MarkObjectAsInvalid() final {
        VMSafePrimitive::MarkObjectAsInvalid();
        prim.MarkObjectAsInvalid();
    }

    [[nodiscard]] bool IsMarkedInvalid() const final { return !prim.IsValid(); }

    // the receiver included
    [[nodiscard]] inline uint8_t GetNumberOfArguments() const final {
        return 2;
    }

private:
    BinaryPrim prim;
};

class VMSafeTernaryPrimitive : public VMSafePrimitive {
    friend class FructusImage;

public:
    typedef GCSafeTernaryPrimitive Stored;

    VMSafeTernaryPrimitive(VMSymbol* sig, TernaryPrim prim)
        : VMSafePrimitive(sig), prim(prim) {}

    [[nodiscard]] inline size_t GetObjectSize() const override {
        return sizeof(VMSafeTernaryPrimitive);
    }

    gc_oop_t* Invoke(VMFrame* /*frame*/, gc_oop_t* /*sp*/) override;
    gc_oop_t* Invoke1(VMFrame* /*unused*/, gc_oop_t* /*sp*/) override;

    [[nodiscard]] AbstractVMObject* CloneForMovingGC() const final;

    void MarkObjectAsInvalid() final {
        VMSafePrimitive::MarkObjectAsInvalid();
        prim.MarkObjectAsInvalid();
    }

    [[nodiscard]] bool IsMarkedInvalid() const final { return !prim.IsValid(); }

    // the receiver included
    [[nodiscard]] inline uint8_t GetNumberOfArguments() const final {
        return 3;
    }

private:
    TernaryPrim prim;
};
