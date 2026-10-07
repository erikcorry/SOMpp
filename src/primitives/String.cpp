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

#include "String.h"

#include "FructusPrimitives.h"

#include <ctype.h>
#include <cstdint>
#include <stdio.h>
#include <string.h>

#include "../misc/defs.h"
#include "../vm/Globals.h"
#ifndef FRUCTUS
  #include "../vm/Symbols.h"
#endif
#include "../vm/Universe.h"
#include "../vmobjects/ObjectFormats.h"
#ifndef FRUCTUS
#include "../vmobjects/VMBigInteger.h"  // NOLINT(misc-include-cleaner)
#endif
#include "../vmobjects/VMFrame.h"
#include "../vmobjects/VMString.h"
#include "../vmobjects/VMSymbol.h"  // NOLINT(misc-include-cleaner) it's required to make the types complete

vm_oop_t strConcatenate_(vm_oop_t leftObj, vm_oop_t rightObj) {
    auto* arg = static_cast<VMString*>(rightObj);
    auto* self = static_cast<VMString*>(leftObj);
    size_t const selfLength = self->GetStringLength();
    size_t const argLength = arg->GetStringLength();

    VMString* result = Universe::NewString(selfLength + argLength, nullptr);
    if (result == nullptr) {
        return nullptr;
    }
    memcpy(result->GetRawChars(), self->GetRawChars(), selfLength);
    memcpy(result->GetRawChars() + selfLength, arg->GetRawChars(), argLength);
    return result;
}

#ifndef FRUCTUS
// fructus makes no symbols at run time
static vm_oop_t strAsSymbol(vm_oop_t rcvr) {
    auto* self = static_cast<VMString*>(rcvr);
    return SymbolFor(self->GetRawChars(), self->GetStringLength());
}
#endif

vm_oop_t strHashcode(vm_oop_t rcvr) {
    auto* self = static_cast<VMString*>(rcvr);
    return NEW_INT(self->GetHash());
}

vm_oop_t strLength(vm_oop_t rcvr) {
    auto* self = static_cast<VMString*>(rcvr);

    size_t const len = self->GetStringLength();
    return NEW_INT((som_int_t)len);
}

vm_oop_t strEqual(vm_oop_t leftObj, vm_oop_t op1) {
    auto* left = static_cast<VMString*>(leftObj);

    if (IS_TAGGED(op1)) {
        return load_ptr(falseObject);
    }

    if (leftObj == op1) {
        return load_ptr(trueObject);
    }

    VMClass const* const otherClass = CLASS_OF(op1);
#ifdef FRUCTUS
    // a symbol is only ever equal to itself, never to a string
    if (otherClass != load_ptr(stringClass) ||
        left->GetClass() != load_ptr(stringClass)) {
        return load_ptr(falseObject);
    }
#else
    if (otherClass != load_ptr(stringClass) &&
        otherClass != load_ptr(symbolClass)) {
        return load_ptr(falseObject);
    }
#endif

    auto* right = static_cast<VMString*>(op1);
    if (left->length != right->length) {
        return load_ptr(falseObject);
    }

    if (strncmp(left->chars, right->chars, left->length) == 0) {
        return load_ptr(trueObject);
    }
    return load_ptr(falseObject);
}

vm_oop_t strPrimSubstringFromTo(vm_oop_t rcvr,
                                       vm_oop_t start,
                                       vm_oop_t end) {
    auto* self = static_cast<VMString*>(rcvr);
    som_int_t const s = SMALL_INT_VAL(start) - 1;
    som_int_t const e = SMALL_INT_VAL(end) - 1;
    som_int_t const length = (som_int_t)self->GetStringLength();

    if (s < 0 || s > length || e < s - 1 || e >= length) {
        return Universe::NewString("Error - index out of bounds");
    }
    return Universe::NewString(e - s + 1, self->GetRawChars() + s);
}

vm_oop_t strCharAt(vm_oop_t rcvr, vm_oop_t indexPtr) {
    auto* self = static_cast<VMString*>(rcvr);
    som_int_t const index = SMALL_INT_VAL(indexPtr) - 1;

    if (unlikely(index < 0 || (size_t)index >= self->GetStringLength())) {
        return Universe::NewString("Error - index out of bounds");
    }

    return Universe::NewString(1, &self->GetRawChars()[index]);
}

vm_oop_t strIsWhiteSpace(vm_oop_t rcvr) {
    auto* self = static_cast<VMString*>(rcvr);

    size_t const len = self->GetStringLength();

    const char* string = self->GetRawChars();

    for (size_t i = 0; i < len; i++) {
        if (isspace(string[i]) == 0) {
            return load_ptr(falseObject);
        }
    }

    if (len > 0) {
        return load_ptr(trueObject);
    }
    return load_ptr(falseObject);
}

vm_oop_t strIsLetters(vm_oop_t rcvr) {
    auto* self = static_cast<VMString*>(rcvr);

    size_t const len = self->GetStringLength();

    const char* string = self->GetRawChars();

    for (size_t i = 0; i < len; i++) {
        if (isalpha(string[i]) == 0) {
            return load_ptr(falseObject);
        }
    }

    if (len > 0) {
        return load_ptr(trueObject);
    }
    return load_ptr(falseObject);
}

vm_oop_t strIsDigits(vm_oop_t rcvr) {
    auto* self = static_cast<VMString*>(rcvr);

    size_t const len = self->GetStringLength();

    const char* string = self->GetRawChars();

    for (size_t i = 0; i < len; i++) {
        if (isdigit(string[i]) == 0) {
            return load_ptr(falseObject);
        }
    }

    if (len > 0) {
        return load_ptr(trueObject);
    }
    return load_ptr(falseObject);
}

// fructus has its primitives in the image, see FructusPrimitives.h
#ifndef FRUCTUS
_String::_String() {
    Add("concatenate:", &strConcatenate_, false);
    Add("asSymbol", &strAsSymbol, false);
    Add("hashcode", &strHashcode, false);
    Add("length", &strLength, false);
    Add("=", &strEqual, false);
    Add("primSubstringFrom:to:", &strPrimSubstringFromTo, false);
    Add("isWhiteSpace", &strIsWhiteSpace, false);
    Add("isLetters", &strIsLetters, false);
    Add("isDigits", &strIsDigits, false);

    Add("charAt:", &strCharAt, false);
}
#endif
