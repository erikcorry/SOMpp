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

#include "PrimitiveLoader.h"

#include <string.h>

#include "../primitives/Array.h"
#include "../primitives/Block.h"
#include "../primitives/Class.h"
#ifndef FRUCTUS
#include "../primitives/Double.h"
#endif
#include "../primitives/Integer.h"
#include "../primitives/Method.h"
#include "../primitives/Object.h"
#include "../primitives/Primitive.h"
#include "../primitives/String.h"
#include "../primitives/Symbol.h"
#include "../primitives/System.h"
#if USE_VECTOR_PRIMITIVES
  #include "../primitives/Vector.h"
#endif
#include "../vmobjects/ObjectFormats.h"
#include "PrimitiveContainer.h"

PrimitiveLoader* PrimitiveLoader::loader = nullptr;

PrimitiveLoader* PrimitiveLoader::GetInstance() {
    if (loader == nullptr) {
        loader = new PrimitiveLoader();
    }
    return loader;
}

PrimitiveLoader::PrimitiveLoader() {
    addPrimitiveObject("Array", new _Array());
#if USE_VECTOR_PRIMITIVES
    addPrimitiveObject("Vector", new _Vector());
#endif
    addPrimitiveObject("Block", new _Block());
    addPrimitiveObject("Class", new _Class());
#ifndef FRUCTUS
    addPrimitiveObject("Double", new _Double());
#endif
    addPrimitiveObject("Integer", new _Integer());
    addPrimitiveObject("Method", new _Method());
    addPrimitiveObject("Object", new _Object());
    addPrimitiveObject("Primitive", new _Primitive());
    addPrimitiveObject("String", new _String());
    addPrimitiveObject("Symbol", new _Symbol());
    addPrimitiveObject("System", new _System());
}

PrimitiveLoader::~PrimitiveLoader() {
    for (const auto& p : primitiveObjects) {
        delete p.container;
    }
    primitiveObjects.Free();
}

void PrimitiveLoader::addPrimitiveObject(const char* name,
                                         PrimitiveContainer* prim) {
    primitiveObjects.push_back({name, prim});
}

PrimitiveContainer* PrimitiveLoader::getObject(const char* name,
                                               size_t length) {
    for (const auto& p : primitiveObjects) {
        if (strncmp(p.name, name, length) == 0 && p.name[length] == '\0') {
            return p.container;
        }
    }
    return nullptr;
}

bool PrimitiveLoader::SupportsClass(const char* name, size_t length) {
    return GetInstance()->getObject(name, length) != nullptr;
}

void PrimitiveLoader::InstallPrimitives(const char* name, size_t length,
                                        VMClass* clazz, bool classSide,
                                        bool showWarning) {
    PrimitiveContainer* container = GetInstance()->getObject(name, length);
    if (container != nullptr) {
        container->InstallPrimitives(clazz, classSide, showWarning);
    }
}
