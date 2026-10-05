/*
 * Integer primitives for fructus, where there are no big integers and no
 * doubles. An integer is a small one when it fits, and otherwise a boxed
 * VMInteger with 64 bits, which wrap around, see som_box_int_t: there is no
 * overflow. Each primitive has a fast path for small integers, and does the
 * rest with 64 bits, signed where it matters.
 */
#include <stdio.h>

#include "../misc/ParseInteger.h"
#include "../vm/Globals.h"
#include "../vm/Print.h"
#include "../vm/Universe.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMArray.h"
#include "../vmobjects/VMInteger.h"
#include "../vmobjects/VMString.h"
#include "FructusPrimitives.h"

static inline vm_oop_t asBool(bool value) {
    return value ? load_ptr(trueObject) : load_ptr(falseObject);
}

/** The value of an argument that has to be an integer. */
static som_box_int_t intArg(vm_oop_t obj) {
    if (unlikely(!IsInteger(obj))) {
        ErrorExit("Integer primitive: the argument is not an Integer");
    }
    return IntegerValue(obj);
}

/** The value of an argument that has to be a small integer. */
static inline som_int_t smallIntArg(vm_oop_t obj) {
    if (unlikely(!IS_SMALL_INT(obj))) {
        ErrorExit("Integer primitive: the argument is not a small Integer");
    }
    return SMALL_INT_VAL(obj);
}

static inline bool bothSmall(vm_oop_t leftObj, vm_oop_t rightObj) {
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    return IS_TAGGED((uintptr_t)leftObj & (uintptr_t)rightObj);
}

static inline void checkDivisor(som_box_int_t right) {
    if (right == 0) {
        ErrorExit("division by zero");
    }
}

// small integers have a bit less than som_int_t, so their sums and
// differences fit in one, and NEW_INT boxes those that don't fit in a small
// integer

vm_oop_t intPlus(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        return NEW_INT(SMALL_INT_VAL(leftObj) + SMALL_INT_VAL(rightObj));
    }
    return NEW_INT(WrappingAdd(IntegerValue(leftObj), intArg(rightObj)));
}

vm_oop_t intMinus(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        return NEW_INT(SMALL_INT_VAL(leftObj) - SMALL_INT_VAL(rightObj));
    }
    som_box_int_t result = 0;
    (void)__builtin_sub_overflow(IntegerValue(leftObj), intArg(rightObj),
                                 &result);
    return NEW_INT(result);
}

vm_oop_t intStar(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        som_int_t result = 0;
        if (!__builtin_mul_overflow(SMALL_INT_VAL(leftObj),
                                    SMALL_INT_VAL(rightObj), &result)) {
            return NEW_INT(result);
        }
    }
    som_box_int_t result = 0;
    (void)__builtin_mul_overflow(IntegerValue(leftObj), intArg(rightObj),
                                 &result);
    return NEW_INT(result);
}

// the quotient of the smallest small integer and -1 fits in som_int_t, and
// in 64 bits it wraps around to the smallest value again, with remainder 0

vm_oop_t intSlash(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        som_int_t const right = SMALL_INT_VAL(rightObj);
        checkDivisor(right);
        return NEW_INT(SMALL_INT_VAL(leftObj) / right);
    }
    som_box_int_t const left = IntegerValue(leftObj);
    som_box_int_t const right = intArg(rightObj);
    checkDivisor(right);
    return NEW_INT(right == -1 ? WrappingAdd(~left, 1) : left / right);
}

vm_oop_t intRem(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        som_int_t const right = SMALL_INT_VAL(rightObj);
        checkDivisor(right);
        return NEW_INT(SMALL_INT_VAL(leftObj) % right);
    }
    som_box_int_t const left = IntegerValue(leftObj);
    som_box_int_t const right = intArg(rightObj);
    checkDivisor(right);
    return NEW_INT(right == -1 ? 0 : left % right);
}

/** The modulo, which has the sign of the divisor. */
vm_oop_t intPercent(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        som_int_t const right = SMALL_INT_VAL(rightObj);
        checkDivisor(right);
        som_int_t result = SMALL_INT_VAL(leftObj) % right;
        if ((result != 0) && ((result < 0) != (right < 0))) {
            result += right;
        }
        return NEW_INT(result);
    }
    som_box_int_t const left = IntegerValue(leftObj);
    som_box_int_t const right = intArg(rightObj);
    checkDivisor(right);
    som_box_int_t result = right == -1 ? 0 : left % right;
    if ((result != 0) && ((result < 0) != (right < 0))) {
        result += right;
    }
    return NEW_INT(result);
}

vm_oop_t intBitwiseXor(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        return NEW_INT(SMALL_INT_VAL(leftObj) ^ SMALL_INT_VAL(rightObj));
    }
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    return NEW_INT(IntegerValue(leftObj) ^ intArg(rightObj));
}

vm_oop_t intAnd(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        return NEW_INT(SMALL_INT_VAL(leftObj) & SMALL_INT_VAL(rightObj));
    }
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    return NEW_INT(IntegerValue(leftObj) & intArg(rightObj));
}

constexpr som_box_int_t boxBits = sizeof(som_box_int_t) * 8;

/** Shifting by 64 or more shifts out all the bits. */
vm_oop_t intLeftShift(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_box_int_t const right = intArg(rightObj);
    if (right < 0) {
        ErrorExit("negative shift");
    }
    if (bothSmall(leftObj, rightObj) && right < SOM_INT_BITS) {
        som_int_t const left = SMALL_INT_VAL(leftObj);
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        som_int_t const result = (som_int_t)((som_uint_t)left << right);
        if ((result >> right) == left) {  // NOLINT(hicpp-signed-bitwise)
            return NEW_INT(result);
        }
    }
    if (right >= boxBits) {
        return NEW_INT(0);
    }
    return NEW_INT(
        (som_box_int_t)((uint64_t)IntegerValue(leftObj) << (unsigned)right));
}

/** The 64 bits of a boxed integer, shifted right. */
vm_oop_t intUnsignedRightShift(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_box_int_t const right = intArg(rightObj);
    if (right < 0) {
        ErrorExit("negative shift");
    }
    if (IS_SMALL_INT(leftObj) && SMALL_INT_VAL(leftObj) >= 0) {
        return right >= SOM_INT_BITS
                   ? NEW_INT(0)
                   // NOLINTNEXTLINE(hicpp-signed-bitwise)
                   : NEW_INT(SMALL_INT_VAL(leftObj) >> right);
    }
    if (right >= boxBits) {
        return NEW_INT(0);
    }
    return NEW_INT(
        (som_box_int_t)((uint64_t)IntegerValue(leftObj) >> (unsigned)right));
}

// NEW_INT only boxes what isn't a small integer, so equal integers are both
// small, or both boxed, with the same value

vm_oop_t intEqual(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        return asBool(leftObj == rightObj);
    }
    return asBool(IsInteger(rightObj) &&
                  IntegerValue(leftObj) == IntegerValue(rightObj));
}

vm_oop_t intUnequal(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        return asBool(leftObj != rightObj);
    }
    return asBool(!IsInteger(rightObj) ||
                  IntegerValue(leftObj) != IntegerValue(rightObj));
}

/**
 * -1, 0 or 1 as the receiver is lower than, equal to or greater than the
 * argument. Small integers compare as their tagged words.
 */
static inline int compare(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (bothSmall(leftObj, rightObj)) {
        auto const left = (som_int_t)leftObj;
        auto const right = (som_int_t)rightObj;
        return (int)(left > right) - (int)(left < right);
    }
    som_box_int_t const left = IntegerValue(leftObj);
    som_box_int_t const right = intArg(rightObj);
    return (int)(left > right) - (int)(left < right);
}

vm_oop_t intLowerthan(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(compare(leftObj, rightObj) < 0);
}

vm_oop_t intLowerThanEqual(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(compare(leftObj, rightObj) <= 0);
}

vm_oop_t intGreaterThan(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(compare(leftObj, rightObj) > 0);
}

vm_oop_t intGreaterThanEqual(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(compare(leftObj, rightObj) >= 0);
}

vm_oop_t intMin(vm_oop_t leftObj, vm_oop_t rightObj) {
    return compare(leftObj, rightObj) > 0 ? rightObj : leftObj;
}

vm_oop_t intMax(vm_oop_t leftObj, vm_oop_t rightObj) {
    return compare(leftObj, rightObj) < 0 ? rightObj : leftObj;
}

/** The smallest 64-bit integer is its own negation. */
vm_oop_t intAbs(vm_oop_t self) {
    som_box_int_t const value = IntegerValue(self);
    return value < 0 ? NEW_INT(WrappingAdd(~value, 1)) : self;
}

vm_oop_t intAsString(vm_oop_t self) {
    if (IS_SMALL_INT(self)) {
        char buffer[8];  // "-16384" and the terminator
        int const length =
            snprintf(buffer, sizeof(buffer), "%d", (int)SMALL_INT_VAL(self));
        return Universe::NewString(length, buffer);
    }
    // the digits from the end, of the magnitude, which fits unsigned
    char buffer[21];  // "-9223372036854775808"
    char* const end = buffer + sizeof(buffer);
    char* start = end;
    som_box_int_t const value = IntegerValue(self);
    uint64_t magnitude = value < 0 ? -(uint64_t)value : (uint64_t)value;
    do {
        *--start = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0);
    if (value < 0) {
        *--start = '-';
    }
    return Universe::NewString(end - start, start);
}

vm_oop_t intFromString(vm_oop_t /*unused*/, vm_oop_t right) {
    auto* str = (VMString*)right;
    return ParseInteger(str->GetRawChars(), str->GetStringLength(), false);
}

/** xorshift, there is no rand() */
static som_uint_t randomState = 0x2545U;

vm_oop_t intAtRandom(vm_oop_t self) {
    som_int_t const bound = smallIntArg(self);
    if (bound <= 0) {
        ErrorExit("atRandom needs a positive receiver");
    }
    // NOLINTBEGIN(hicpp-signed-bitwise)
    randomState ^= randomState << 7U;
    randomState ^= randomState >> 9U;
    randomState ^= randomState << 8U;
    // NOLINTEND(hicpp-signed-bitwise)
    return NEW_INT((som_int_t)((randomState >> 1U) % (som_uint_t)bound));
}

/** An array can't have more elements than a small integer counts. */
vm_oop_t intRange(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t const left = smallIntArg(leftObj);
    som_int_t const right = smallIntArg(rightObj);

    som_int_t const numInteger = right < left ? 0 : right - left + 1;
    VMArray* arr = Universe::NewArray(numInteger);

    size_t index = 0;
    for (som_int_t i = left; i <= right; i += 1, index += 1) {
        arr->SetIndexableField(index, NEW_INT(i));
    }
    return arr;
}
