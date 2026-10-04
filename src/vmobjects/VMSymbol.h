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
#include "../vm/FructusImage.h"
#include "ObjectFormats.h"
#include "VMClass.h"
#include "VMObject.h"
#include "VMString.h"

#ifdef FRUCTUS
/**
 * A selector, or the name of a global, which on fructus isn't an object but
 * a number the image writer gives it, so a VMSymbol* is never dereferenced.
 * The numbers are below FructusLayout::selectorLimit, where no object is,
 * so the GC tests for them with IsSymbol() and leaves them alone. See
 * vm/FructusImage.h.
 */
class VMSymbol {
public:
    typedef GCSymbol Stored;

    /** Whether `obj` is a selector. */
    static bool IsSymbol(const void* obj) {
        return (uintptr_t)obj < FructusLayout::selectorLimit;
    }

    VMSymbol() = delete;
};
#else
class VMSymbol : public VMString {
public:
    typedef GCSymbol Stored;

    VMSymbol(size_t length, const char* str);
    [[nodiscard]] size_t GetObjectSize() const override;
    [[nodiscard]] VMSymbol* CloneForMovingGC() const override;
    [[nodiscard]] VMClass* GetClass() const override;

    DECLARE_AS_DEBUG_STRING(override)

private:
    const uint8_t numberOfArgumentsOfSignature;

    friend class Signature;
    friend class FructusImage;
};
#endif
