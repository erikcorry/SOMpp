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
#include "ObjectFormats.h"
#include "VMClass.h"
#include "VMObject.h"
#include "VMString.h"

#ifdef FRUCTUS
// all symbols, which the linker gathers, see vm/FructusImage.h
extern "C" const char __start_som_symbols[];
extern "C" const char __stop_som_symbols[];

/**
 * A symbol on fructus is only for dispatch and field names, and is only in
 * the image, see vm/FructusImage.h. The program can't have one, so it isn't
 * an object: it has no vtable and no GC field, and isn't marked. The GC
 * tests for symbols with IsSymbol() and walks their cache with
 * WalkObjects().
 */
class VMSymbol {
public:
    typedef GCSymbol Stored;

    /** Whether `obj` points at a symbol. */
    static bool IsSymbol(const void* obj) {
        return (const char*)obj >= __start_som_symbols &&
               (const char*)obj < __stop_som_symbols;
    }

    /** Not virtual, see IsSymbol(). */
    void WalkObjects(walk_heap_fn walk);

    [[nodiscard]] size_t GetStringLength() const { return length; }

    /** The chars follow the symbol, and aren't null-terminated. */
    [[nodiscard]] const char* GetRawChars() const {
        return (const char*)(this + 1);
    }

private:
    GCClass* cachedClass_invokable[3];
    size_t nextCachePos;
    GCInvokable* cachedInvokable[3];
    uint8_t numberOfArgumentsOfSignature;
    uint8_t length;

    inline VMInvokable* GetCachedInvokable(const VMClass* cls) const {
        if (cls == load_ptr(cachedClass_invokable[0])) {
            return load_ptr(cachedInvokable[0]);
        }

        if (cls == load_ptr(cachedClass_invokable[1])) {
            return load_ptr(cachedInvokable[1]);
        }

        if (cls == load_ptr(cachedClass_invokable[2])) {
            return load_ptr(cachedInvokable[2]);
        }
        return nullptr;
    }

    void UpdateCachedInvokable(VMClass* cls, VMInvokable* invo);

    friend class Signature;
    friend class VMClass;
    friend class FructusImage;
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

    void WalkObjects(walk_heap_fn /*unused*/) override;

private:
    const uint8_t numberOfArgumentsOfSignature;
    GCClass* cachedClass_invokable[3]{};
    size_t nextCachePos{0};
    GCInvokable* cachedInvokable[3]{};

    inline VMInvokable* GetCachedInvokable(const VMClass* cls) const {
        if (cls == load_ptr(cachedClass_invokable[0])) {
            return load_ptr(cachedInvokable[0]);
        }

        if (cls == load_ptr(cachedClass_invokable[1])) {
            return load_ptr(cachedInvokable[1]);
        }

        if (cls == load_ptr(cachedClass_invokable[2])) {
            return load_ptr(cachedInvokable[2]);
        }
        return nullptr;
    }

    void UpdateCachedInvokable(VMClass* cls, VMInvokable* invo);

    friend class Signature;
    friend class VMClass;
    friend class FructusImage;
};
#endif
