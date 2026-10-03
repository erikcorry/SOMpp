#include "ParseInteger.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../vm/Universe.h"  // NOLINT(misc-include-cleaner)
#include "../vmobjects/ObjectFormats.h"
#ifndef FRUCTUS
  #include "../vmobjects/VMBigInteger.h"  // NOLINT(misc-include-cleaner)
#endif

#ifdef FRUCTUS
// without big integers, anything that does not fit is an error anyway
using parse_int_t = som_int_t;
#else
using parse_int_t = int64_t;
#endif

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

    // accumulate negatively, so that the most negative value fits
    parse_int_t value = 0;
    size_t const firstDigit = i;
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
#ifdef FRUCTUS
        ErrorExit("integer literal out of range");
#else
        char* const buffer = (char*)malloc(length + 1);
        memcpy(buffer, str, length);
        buffer[length] = '\0';
        vm_oop_t const result =
            Universe::NewBigIntegerFromStr(buffer, negateValue);
        free(buffer);
        return result;
#endif
    }

    return NEW_INT(value);
}

vm_oop_t ParseInteger(const char* str, bool negateValue) {
    return ParseInteger(str, strlen(str), negateValue);
}
