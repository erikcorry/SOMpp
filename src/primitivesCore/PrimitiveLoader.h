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

#include <stddef.h>

#include "../misc/SimpleVector.h"
#include "../misc/defs.h"
#include "Primitives.h"

class PrimitiveContainer;

class PrimitiveLoader {
public:
    PrimitiveLoader();
    virtual ~PrimitiveLoader();

    static bool SupportsClass(const char* name, size_t length);
    static void InstallPrimitives(const char* name, size_t length,
                                  VMClass* clazz, bool classSide,
                                  bool showWarning);

private:
    struct Entry {
        const char* name;  // a string literal
        PrimitiveContainer* container;
    };

    // created on first use, because static constructors are not run on all
    // targets
    static PrimitiveLoader* GetInstance();

    void addPrimitiveObject(const char* name, PrimitiveContainer* prim);
    PrimitiveContainer* getObject(const char* name, size_t length);

    SimpleVector<Entry> primitiveObjects;
    static PrimitiveLoader* loader;
};
