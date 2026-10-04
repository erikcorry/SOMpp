/*
 * Integer primitives for fructus: only (tagged) small integers, there are no
 * big integers and no doubles. Results that do not fit are an error.
 */
#include <stdio.h>

#include "../misc/ParseInteger.h"
#include "../vm/Globals.h"
#include "../vm/Print.h"
#include "../vm/Universe.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMArray.h"
#include "../vmobjects/VMString.h"
#include "FructusPrimitives.h"

static inline vm_oop_t asBool(bool value) {
    return value ? load_ptr(trueObject) : load_ptr(falseObject);
}

/** The value of an argument that has to be a small integer. */
static inline som_int_t intArg(vm_oop_t obj) {
    if (unlikely(!IS_SMALL_INT(obj))) {
        ErrorExit("Integer primitive: the argument is not an Integer");
    }
    return SMALL_INT_VAL(obj);
}

vm_oop_t intPlus(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t result = 0;
    if (__builtin_add_overflow(SMALL_INT_VAL(leftObj), intArg(rightObj),
                               &result)) {
        IntegerOverflow();
    }
    return NEW_INT(result);
}

vm_oop_t intMinus(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t result = 0;
    if (__builtin_sub_overflow(SMALL_INT_VAL(leftObj), intArg(rightObj),
                               &result)) {
        IntegerOverflow();
    }
    return NEW_INT(result);
}

vm_oop_t intStar(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t result = 0;
    if (__builtin_mul_overflow(SMALL_INT_VAL(leftObj), intArg(rightObj),
                               &result)) {
        IntegerOverflow();
    }
    return NEW_INT(result);
}

vm_oop_t intSlash(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t const right = intArg(rightObj);
    if (right == 0) {
        ErrorExit("division by zero");
    }
    return NEW_INT(SMALL_INT_VAL(leftObj) / right);
}

vm_oop_t intRem(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t const right = intArg(rightObj);
    if (right == 0) {
        ErrorExit("division by zero");
    }
    return NEW_INT(SMALL_INT_VAL(leftObj) % right);
}

vm_oop_t intPercent(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t const right = intArg(rightObj);
    if (right == 0) {
        ErrorExit("division by zero");
    }
    som_int_t result = SMALL_INT_VAL(leftObj) % right;
    if ((result != 0) && ((result < 0) != (right < 0))) {
        result += right;
    }
    return NEW_INT(result);
}

vm_oop_t intBitwiseXor(vm_oop_t leftObj, vm_oop_t rightObj) {
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    return NEW_INT(SMALL_INT_VAL(leftObj) ^ intArg(rightObj));
}

vm_oop_t intAnd(vm_oop_t leftObj, vm_oop_t rightObj) {
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    return NEW_INT(SMALL_INT_VAL(leftObj) & intArg(rightObj));
}

vm_oop_t intLeftShift(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t const left = SMALL_INT_VAL(leftObj);
    som_int_t const right = intArg(rightObj);
    if (right < 0 || right >= SOM_INT_BITS) {
        if (left != 0) {
            IntegerOverflow();
        }
        return leftObj;
    }
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    som_int_t const result = (som_int_t)((som_uint_t)left << right);
    if ((result >> right) != left) {  // NOLINT(hicpp-signed-bitwise)
        IntegerOverflow();
    }
    return NEW_INT(result);
}

vm_oop_t intUnsignedRightShift(vm_oop_t leftObj, vm_oop_t rightObj) {
    auto const left = (som_uint_t)SMALL_INT_VAL(leftObj);
    som_int_t const right = intArg(rightObj);
    if (right < 0 || right >= SOM_INT_BITS) {
        return NEW_INT(0);
    }
    // a result with the top bit set does not fit, NEW_INT reports that
    return NEW_INT((som_int_t)(left >> right));
}

vm_oop_t intEqual(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(IS_SMALL_INT(rightObj) &&
                  SMALL_INT_VAL(leftObj) == SMALL_INT_VAL(rightObj));
}

vm_oop_t intUnequal(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(!IS_SMALL_INT(rightObj) ||
                  SMALL_INT_VAL(leftObj) != SMALL_INT_VAL(rightObj));
}

vm_oop_t intLowerthan(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(SMALL_INT_VAL(leftObj) < intArg(rightObj));
}

vm_oop_t intLowerThanEqual(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(SMALL_INT_VAL(leftObj) <= intArg(rightObj));
}

vm_oop_t intGreaterThan(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(SMALL_INT_VAL(leftObj) > intArg(rightObj));
}

vm_oop_t intGreaterThanEqual(vm_oop_t leftObj, vm_oop_t rightObj) {
    return asBool(SMALL_INT_VAL(leftObj) >= intArg(rightObj));
}

vm_oop_t intMin(vm_oop_t leftObj, vm_oop_t rightObj) {
    return intArg(rightObj) < SMALL_INT_VAL(leftObj) ? rightObj : leftObj;
}

vm_oop_t intMax(vm_oop_t leftObj, vm_oop_t rightObj) {
    return intArg(rightObj) > SMALL_INT_VAL(leftObj) ? rightObj : leftObj;
}

vm_oop_t intAbs(vm_oop_t self) {
    som_int_t const value = SMALL_INT_VAL(self);
    // the negated minimum small int does not fit, NEW_INT reports that
    return value < 0 ? NEW_INT(-value) : self;
}

vm_oop_t intAsString(vm_oop_t self) {
    char buffer[8];  // "-16384" and the terminator
    int const length =
        snprintf(buffer, sizeof(buffer), "%d", (int)SMALL_INT_VAL(self));
    return Universe::NewString(length, buffer);
}

vm_oop_t intFromString(vm_oop_t /*unused*/, vm_oop_t right) {
    auto* str = (VMString*)right;
    return ParseInteger(str->GetRawChars(), str->GetStringLength(), false);
}

/** xorshift, there is no rand() */
static som_uint_t randomState = 0x2545U;

vm_oop_t intAtRandom(vm_oop_t self) {
    som_int_t const bound = SMALL_INT_VAL(self);
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

vm_oop_t intRange(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t const left = SMALL_INT_VAL(leftObj);
    som_int_t const right = intArg(rightObj);

    som_int_t const numInteger = right < left ? 0 : right - left + 1;
    VMArray* arr = Universe::NewArray(numInteger);

    size_t index = 0;
    for (som_int_t i = left; i <= right; i += 1, index += 1) {
        arr->SetIndexableField(index, NEW_INT(i));
    }
    return arr;
}
