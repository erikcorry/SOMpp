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

#include "../misc/defs.h"
#include "AbstractObject.h"
#include "IntegerBox.h"

class VMInteger : public AbstractVMObject {
public:
    typedef GCInteger Stored;

    explicit VMInteger(som_box_int_t val) : embeddedInteger(val) {}
    ~VMInteger() override = default;

    [[nodiscard]] inline som_box_int_t GetEmbeddedInteger() const;
    [[nodiscard]] VMInteger* CloneForMovingGC() const override;
    [[nodiscard]] VMClass* GetClass() const override;
    [[nodiscard]] inline size_t GetObjectSize() const override;

    [[nodiscard]] inline som_int_t GetHash() const override {
        return (som_int_t)embeddedInteger;
    }

    void MarkObjectAsInvalid() override;
    [[nodiscard]] bool IsMarkedInvalid() const override;

    DECLARE_AS_DEBUG_STRING(override)

    make_testable(public);

    som_box_int_t embeddedInteger;
};

som_box_int_t VMInteger::GetEmbeddedInteger() const {
    return embeddedInteger;
}

size_t VMInteger::GetObjectSize() const {
    // no need to pad -> sizeof returns padded size anyway
    return sizeof(VMInteger);
}

#ifdef FRUCTUS
/**
 * Whether `obj` is a boxed integer, see som_box_int_t. The integer box is
 * one, so this compares with its vtable.
 */
inline bool IsBoxedInteger(vm_oop_t obj) {
    return !IS_TAGGED(obj) &&
           *(void**)obj == *(void**)GlobalBox::IntegerBox();  // NOLINT
}

inline bool IsInteger(vm_oop_t obj) {
    return IS_TAGGED(obj) || IsBoxedInteger(obj);
}

/** The value of an integer, small or boxed. */
inline som_box_int_t IntegerValue(vm_oop_t obj) {
    return IS_TAGGED(obj)
               ? (som_box_int_t)SMALL_INT_VAL(obj)
               : static_cast<VMInteger*>(obj)->GetEmbeddedInteger();
}

/** a + b, wrapping around. */
inline som_box_int_t WrappingAdd(som_box_int_t a, som_box_int_t b) {
    som_box_int_t result = 0;
    (void)__builtin_add_overflow(a, b, &result);
    return result;
}

/**
 * Integer primitives, for the cases of the interpreter's fast paths that
 * aren't small integers: the receiver and argument must be integers, and
 * either may be boxed. See primitives/IntegerFructus.cpp.
 */
__attribute__((cold)) vm_oop_t IntegerWideAdd(vm_oop_t left, vm_oop_t right);
vm_oop_t IntegerWideGreaterThan(vm_oop_t left, vm_oop_t right);
#endif
