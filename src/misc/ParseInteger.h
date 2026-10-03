#pragma once

#include <stddef.h>

#include "../vmobjects/ObjectFormats.h"
#include "defs.h"

/** Parse an optionally signed decimal integer, as strtoll would. */
vm_oop_t ParseInteger(const char* str, size_t length, bool negateValue);
vm_oop_t ParseInteger(const char* str, bool negateValue);
