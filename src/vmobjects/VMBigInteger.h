#pragma once

#include "../lib/InfInt.h"
#include "../misc/defs.h"
#include "AbstractObject.h"

class VMBigInteger : public AbstractVMObject {
public:
    typedef GCBigInteger Stored;

    explicit VMBigInteger(const char* value, bool negate)
        : embeddedInteger(negate ? -InfInt(value) : InfInt(value)) {
        assert(!embeddedInteger.isWithinSmallIntRange());
    }
    explicit VMBigInteger(int64_t value) : embeddedInteger(InfInt(value)) {
        assert(!embeddedInteger.isWithinSmallIntRange());
    }
    explicit VMBigInteger(const InfInt&& value) : embeddedInteger(value) {
        assert(!embeddedInteger.isWithinSmallIntRange());
    }

    ~VMBigInteger() override = default;

    [[nodiscard]] inline const InfInt* GetEmbeddedInteger() const {
        return &embeddedInteger;
    }

    [[nodiscard]] VMBigInteger* CloneForMovingGC() const override;
    [[nodiscard]] VMClass* GetClass() const override;

    [[nodiscard]] inline size_t GetObjectSize() const override {
        return sizeof(VMBigInteger);
    }

    [[nodiscard]] inline som_int_t GetHash() const override {
        return (som_int_t)embeddedInteger.toInt64();
    }

    void MarkObjectAsInvalid() override;
    [[nodiscard]] bool IsMarkedInvalid() const override;

    DECLARE_AS_DEBUG_STRING(override)

    /* primitive operations */
    [[nodiscard]] vm_oop_t Add(som_int_t /*value*/) const;
    vm_oop_t Add(vm_oop_t /*value*/) const;

    [[nodiscard]] vm_oop_t SubtractFrom(som_int_t /*value*/) const;
    vm_oop_t Subtract(vm_oop_t /*value*/) const;

    [[nodiscard]] vm_oop_t Multiply(som_int_t /*value*/) const;
    vm_oop_t Multiply(vm_oop_t /*value*/) const;

    [[nodiscard]] vm_oop_t DivisionFrom(som_int_t /*value*/) const;
    vm_oop_t DivideBy(vm_oop_t /*value*/) const;

    [[nodiscard]] vm_oop_t ModuloFrom(som_int_t /*value*/) const;
    vm_oop_t Modulo(vm_oop_t /*value*/) const;

    [[nodiscard]] vm_oop_t Negate();

    vm_oop_t IsEqual(VMBigInteger* /*o*/) const;

    /* fields */
    make_testable(public);

    InfInt embeddedInteger;
};
