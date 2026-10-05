/*
 * Integer primitives for fructus, where there are no big integers and no
 * doubles. An integer is a small one when it fits, and otherwise a boxed
 * VMInteger with 64 bits, which wrap around, see som_box_int_t: there is no
 * overflow.
 *
 * Each primitive has a fast path for small integers, mostly on their tagged
 * words, 2v + 1, and otherwise tail calls a stub, which passes what to do to
 * wideOp(), where everything that can involve a boxed integer is, and the
 * checks of the arguments. The stub is there because the fructus ABI depends
 * on the signature: a function of two arguments preserves r2, so it can't
 * tail call one of three, which would make the fast path save r2 and lr.
 */
#include <limits.h>
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

/** The tagged word of a small integer, 2v + 1, and back. */
static inline som_int_t word(vm_oop_t obj) {
    return (som_int_t)obj;
}

static inline vm_oop_t fromWord(som_int_t word) {
    return (vm_oop_t)word;
}

namespace {
enum class Wide : uint8_t {
    Add, Sub, Mul, Div, Rem, Mod, And, Xor, Shl, Shr,
    Lt, Le, Gt, Ge, Eq, Ne, Min, Max, Abs
};
}  // namespace

/**
 * The operation on two integers, either of which may be boxed, or the
 * argument something else, or a small integer the fast path can't do, like a
 * division by zero. The receiver of an Integer primitive is an integer, but
 * the interpreter's is checked too.
 */
__attribute__((noinline)) static vm_oop_t wideOp(vm_oop_t leftObj,
                                                 vm_oop_t rightObj, Wide op) {
    som_box_int_t const left = intArg(leftObj);
    if (op == Wide::Abs) {
        return left < 0 ? NEW_INT(WrappingAdd(~left, 1)) : leftObj;
    }
    if (op == Wide::Eq || op == Wide::Ne) {
        bool const equal = IsInteger(rightObj) && left == IntegerValue(rightObj);
        return asBool(equal == (op == Wide::Eq));
    }
    som_box_int_t const right = intArg(rightObj);
    if (op >= Wide::Div && op <= Wide::Mod && right == 0) {
        ErrorExit("division by zero");
    }
    if ((op == Wide::Shl || op == Wide::Shr) && right < 0) {
        ErrorExit("negative shift");
    }

    som_box_int_t result = 0;
    switch (op) {
        // the overflow builtins wrap, and are signed
        case Wide::Add:
            (void)__builtin_add_overflow(left, right, &result);
            break;
        case Wide::Sub:
            (void)__builtin_sub_overflow(left, right, &result);
            break;
        case Wide::Mul:
            (void)__builtin_mul_overflow(left, right, &result);
            break;
        // the smallest value divided by -1 wraps around to itself, with
        // remainder 0
        case Wide::Div:
            result = right == -1 ? WrappingAdd(~left, 1) : left / right;
            break;
        case Wide::Rem:
            result = right == -1 ? 0 : left % right;
            break;
        case Wide::Mod:
            // the modulo has the sign of the divisor
            result = right == -1 ? 0 : left % right;
            if ((result != 0) && ((result < 0) != (right < 0))) {
                result += right;
            }
            break;
        case Wide::And:
            result = left & right;  // NOLINT(hicpp-signed-bitwise)
            break;
        case Wide::Xor:
            result = left ^ right;  // NOLINT(hicpp-signed-bitwise)
            break;
        // shifting by 64 or more shifts out all the bits, and >>> shifts the
        // 64 bits
        case Wide::Shl:
            result = right >= 64 ? 0
                                 : (som_box_int_t)((uint64_t)left
                                                   << (unsigned)right);
            break;
        case Wide::Shr:
            result = right >= 64 ? 0
                                 : (som_box_int_t)((uint64_t)left >>
                                                   (unsigned)right);
            break;
        default: {
            int const order = (int)(left > right) - (int)(left < right);
            switch (op) {
                case Wide::Lt: return asBool(order < 0);
                case Wide::Le: return asBool(order <= 0);
                case Wide::Gt: return asBool(order > 0);
                case Wide::Ge: return asBool(order >= 0);
                case Wide::Min: return order > 0 ? rightObj : leftObj;
                default: return order < 0 ? rightObj : leftObj;  // Max
            }
        }
    }
    return NEW_INT(result);
}

// the stubs the fast paths tail call, see the top of the file
#define WIDE_STUB(name, op)                                            \
    __attribute__((noinline)) static vm_oop_t name(vm_oop_t leftObj,   \
                                                   vm_oop_t rightObj) { \
        return wideOp(leftObj, rightObj, op);                          \
    }
WIDE_STUB(wideSub, Wide::Sub)
WIDE_STUB(wideMul, Wide::Mul)
WIDE_STUB(wideDiv, Wide::Div)
WIDE_STUB(wideRem, Wide::Rem)
WIDE_STUB(wideMod, Wide::Mod)
WIDE_STUB(wideAnd, Wide::And)
WIDE_STUB(wideXor, Wide::Xor)
WIDE_STUB(wideShl, Wide::Shl)
WIDE_STUB(wideShr, Wide::Shr)
WIDE_STUB(wideLt, Wide::Lt)
WIDE_STUB(wideLe, Wide::Le)
WIDE_STUB(wideGt, Wide::Gt)
WIDE_STUB(wideGe, Wide::Ge)
WIDE_STUB(wideEq, Wide::Eq)
WIDE_STUB(wideNe, Wide::Ne)
WIDE_STUB(wideMin, Wide::Min)
WIDE_STUB(wideMax, Wide::Max)
#undef WIDE_STUB

// for the interpreter too, see VMInteger.h
__attribute__((noinline)) vm_oop_t IntegerWideAdd(vm_oop_t leftObj,
                                                  vm_oop_t rightObj) {
    return wideOp(leftObj, rightObj, Wide::Add);
}

__attribute__((noinline)) vm_oop_t IntegerWideGreaterThan(vm_oop_t leftObj,
                                                          vm_oop_t rightObj) {
    return wideGt(leftObj, rightObj);
}

__attribute__((noinline)) static vm_oop_t wideAbs(vm_oop_t self) {
    return wideOp(self, self, Wide::Abs);
}

// The fast paths. A sum or difference of tagged words that overflows
// som_int_t is one whose small integer doesn't fit, and the tag bits cancel:
// (2a + 1) - 1 + (2b + 1) is 2(a + b) + 1.
//
// At -Os gcc lays out the blocks without looking at their probabilities, so
// the fast path took the branch and the slow path fell through to its tail
// call. Optimized for speed, the fast path falls through to the ret, at no
// cost in size.
#define FAST_PATH __attribute__((optimize("O2")))

FAST_PATH vm_oop_t intPlus(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t result = 0;
    if (likely(bothSmall(leftObj, rightObj)) &&
        !__builtin_add_overflow(word(leftObj) - 1, word(rightObj), &result)) {
        return fromWord(result);
    }
    return IntegerWideAdd(leftObj, rightObj);
}

FAST_PATH vm_oop_t intMinus(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t result = 0;
    if (likely(bothSmall(leftObj, rightObj)) &&
        !__builtin_sub_overflow(word(leftObj), word(rightObj) - 1, &result)) {
        return fromWord(result);
    }
    return wideSub(leftObj, rightObj);
}

/** a * 2b is even, so adding the tag can't overflow. */
FAST_PATH vm_oop_t intStar(vm_oop_t leftObj, vm_oop_t rightObj) {
    som_int_t result = 0;
    if (likely(bothSmall(leftObj, rightObj)) &&
        !__builtin_mul_overflow(SMALL_INT_VAL(leftObj), word(rightObj) - 1,
                                &result)) {
        return fromWord(result + 1);
    }
    return wideMul(leftObj, rightObj);
}

// The smallest small integer divided by -1 doesn't fit in a small integer,
// but does in som_int_t, and NEW_INT boxes it. Division by zero is an error
// of wideOp().

vm_oop_t intSlash(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj)) && SMALL_INT_VAL(rightObj) != 0) {
        return NEW_INT(SMALL_INT_VAL(leftObj) / SMALL_INT_VAL(rightObj));
    }
    return wideDiv(leftObj, rightObj);
}

vm_oop_t intRem(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj)) && SMALL_INT_VAL(rightObj) != 0) {
        return NEW_INT(SMALL_INT_VAL(leftObj) % SMALL_INT_VAL(rightObj));
    }
    return wideRem(leftObj, rightObj);
}

/** The modulo, which has the sign of the divisor. */
vm_oop_t intPercent(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj)) && SMALL_INT_VAL(rightObj) != 0) {
        som_int_t const right = SMALL_INT_VAL(rightObj);
        som_int_t result = SMALL_INT_VAL(leftObj) % right;
        if ((result != 0) && ((result < 0) != (right < 0))) {
            result += right;
        }
        return NEW_INT(result);
    }
    return wideMod(leftObj, rightObj);
}

vm_oop_t intAnd(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return fromWord(word(leftObj) & word(rightObj));  // NOLINT
    }
    return wideAnd(leftObj, rightObj);
}

vm_oop_t intBitwiseXor(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return fromWord((word(leftObj) ^ word(rightObj)) | 1);  // NOLINT
    }
    return wideXor(leftObj, rightObj);
}

/** Shifting a small integer by less than its width, if nothing is lost. */
vm_oop_t intLeftShift(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        som_int_t const left = SMALL_INT_VAL(leftObj);
        som_int_t const right = SMALL_INT_VAL(rightObj);
        if (right >= 0 && right < SOM_INT_BITS) {
            auto const result = (som_int_t)((som_uint_t)left << right);
            if ((result >> right) == left) {  // NOLINT(hicpp-signed-bitwise)
                return NEW_INT(result);
            }
        }
    }
    return wideShl(leftObj, rightObj);
}

/** A negative integer has its 64 bits shifted, see wideOp(). */
vm_oop_t intUnsignedRightShift(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        som_int_t const left = SMALL_INT_VAL(leftObj);
        som_int_t const right = SMALL_INT_VAL(rightObj);
        if (left >= 0 && right >= 0) {
            // the small integers have one bit less than som_int_t
            return NEW_INT(left >> (right < SOM_INT_BITS - 1  // NOLINT
                                        ? right
                                        : SOM_INT_BITS - 1));
        }
    }
    return wideShr(leftObj, rightObj);
}

// Small integers compare as their tagged words, and NEW_INT only boxes what
// isn't a small integer, so equal integers are both small, or both boxed.

vm_oop_t intEqual(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return asBool(leftObj == rightObj);
    }
    return wideEq(leftObj, rightObj);
}

vm_oop_t intUnequal(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return asBool(leftObj != rightObj);
    }
    return wideNe(leftObj, rightObj);
}

vm_oop_t intLowerthan(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return asBool(word(leftObj) < word(rightObj));
    }
    return wideLt(leftObj, rightObj);
}

vm_oop_t intLowerThanEqual(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return asBool(word(leftObj) <= word(rightObj));
    }
    return wideLe(leftObj, rightObj);
}

vm_oop_t intGreaterThan(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return asBool(word(leftObj) > word(rightObj));
    }
    return wideGt(leftObj, rightObj);
}

vm_oop_t intGreaterThanEqual(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return asBool(word(leftObj) >= word(rightObj));
    }
    return wideGe(leftObj, rightObj);
}

vm_oop_t intMin(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return word(rightObj) < word(leftObj) ? rightObj : leftObj;
    }
    return wideMin(leftObj, rightObj);
}

vm_oop_t intMax(vm_oop_t leftObj, vm_oop_t rightObj) {
    if (likely(bothSmall(leftObj, rightObj))) {
        return word(rightObj) > word(leftObj) ? rightObj : leftObj;
    }
    return wideMax(leftObj, rightObj);
}

/** The negated smallest small integer is boxed, and the 64-bit one wraps. */
vm_oop_t intAbs(vm_oop_t self) {
    if (likely(IS_SMALL_INT(self))) {
        som_int_t const value = SMALL_INT_VAL(self);
        return value < 0 ? NEW_INT(-value) : self;
    }
    return wideAbs(self);
}

/**
 * libc's printf has 32 bits at most, so the low digits of a wider value come
 * off nine at a time first. Dividing the signed value uses the same helpers
 * as / and rem:, and the remainders have its sign.
 */
vm_oop_t intAsString(vm_oop_t self) {
    constexpr som_box_int_t billion = 1000000000;
    som_box_int_t value = IntegerValue(self);
    long groups[2];  // 2^63 / 10^18 fits in 32 bits
    int numberOfGroups = 0;
    while (value < LONG_MIN || value > LONG_MAX) {
        groups[numberOfGroups++] = (long)(value % billion);
        value /= billion;
    }

    char buffer[22];  // "-9223372036854775808" and the terminator
    int length = snprintf(buffer, sizeof(buffer), "%ld", (long)value);
    while (numberOfGroups > 0) {
        long const group = groups[--numberOfGroups];
        length += snprintf(buffer + length, sizeof(buffer) - length, "%09ld",
                           group < 0 ? -group : group);
    }
    return Universe::NewString(length, buffer);
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
