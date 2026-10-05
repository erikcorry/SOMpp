#include "ParseInteger.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../vm/Universe.h"  // NOLINT(misc-include-cleaner)
#include "../vmobjects/ObjectFormats.h"
#ifndef FRUCTUS
  #include "../vmobjects/VMBigInteger.h"  // NOLINT(misc-include-cleaner)
#endif

using parse_int_t = int64_t;

vm_oop_t ParseInteger(const char* str, size_t length, bool negateValue) {
    size_t i = 0;
    while (i < length && (str[i] == ' ' || (str[i] >= '\t' && str[i] <= '\r'))) {
        i++;
    }

    bool negative = negateValue;
    if (i < length && (str[i] == '-' || str[i] == '+')) {
        negative = negative != (str[i] == '-');
        i++;
    }

    size_t const firstDigit = i;
#ifdef FRUCTUS
    // there are no big integers, so the value wraps around, see
    // som_box_int_t
    uint64_t magnitude = 0;
    for (; i < length && str[i] >= '0' && str[i] <= '9'; i++) {
        magnitude = magnitude * 10 + (uint64_t)(str[i] - '0');
    }
    if (i == firstDigit) {
        return NEW_INT(0);
    }
    return NEW_INT((som_box_int_t)(negative ? -magnitude : magnitude));
#else
    // accumulate negatively, so that the most negative value fits
    parse_int_t value = 0;
    bool overflow = false;
    for (; i < length && str[i] >= '0' && str[i] <= '9'; i++) {
        overflow = overflow || __builtin_mul_overflow(value, 10, &value) ||
                   __builtin_sub_overflow(value, str[i] - '0', &value);
    }

    if (i == firstDigit) {
        // did not parse anything
        return NEW_INT(0);
    }

    if (!overflow && !negative) {
        overflow = __builtin_sub_overflow((parse_int_t)0, value, &value);
    }

    if (overflow) {
        char* const buffer = (char*)malloc(length + 1);
        memcpy(buffer, str, length);
        buffer[length] = '\0';
        vm_oop_t const result =
            Universe::NewBigIntegerFromStr(buffer, negateValue);
        free(buffer);
        return result;
    }

    return NEW_INT(value);
#endif
}

vm_oop_t ParseInteger(const char* str, bool negateValue) {
    return ParseInteger(str, strlen(str), negateValue);
}
