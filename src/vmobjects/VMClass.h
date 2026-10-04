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
#include "../vm/Globals.h"
#include "../vm/IsValidObject.h"
#include "VMObject.h"

#if defined(_MSC_VER)  // Visual Studio
  #include <windows.h>

  #include "../primitives/Core.h"
#endif

class ClassGenerationContext;

#ifndef FRUCTUS
enum PrimInstallResult : uint8_t {
    NULL_ARG,
    NULL_IN_INVOKABLES,
    HASH_MISMATCH,
    INSTALLED_REPLACED,
    INSTALLED_ADDED
};
#endif

class VMClass : public VMObject {
    friend class FructusImage;

public:
    typedef GCClass Stored;

    VMClass();
    VMClass(size_t numberOfFields, size_t additionalBytes);

    [[nodiscard]] VMObject* GetSuperClass() const;
    void SetSuperClass(VMObject* sup);
    [[nodiscard]] bool HasSuperClass() const;
    [[nodiscard]] VMSymbol* GetName() const;
    void SetName(VMSymbol* name);
#ifndef FRUCTUS
    // fructus has only the number of fields, see numberOfInstanceFields
    [[nodiscard]] VMArray* GetInstanceFields() const;
    void SetInstanceFields(VMArray* instFields);
#endif
    [[nodiscard]] VMArray* GetInstanceInvokables() const;
#ifndef FRUCTUS
    void SetInstanceInvokables(VMArray* /*invokables*/);
#endif
    [[nodiscard]] size_t GetNumberOfInstanceInvokables() const;
    [[nodiscard]] VMInvokable* GetInstanceInvokable(size_t index) const;
#ifndef FRUCTUS
    void SetInstanceInvokable(size_t index, VMInvokable* invokable);
#endif
    VMInvokable* LookupInvokable(VMSymbol* name);
#ifndef FRUCTUS
    // fructus has no reflection on field names
    som_int_t LookupFieldIndex(VMSymbol* name) const;
#endif
#ifndef FRUCTUS
    // fructus has its primitives in the image, see primitives/FructusPrimitives.h
    PrimInstallResult InstallPrimitive(VMInvokable* invokable,
                                       size_t bytecodeHash,
                                       bool reportHashMismatch);
#endif
#ifndef FRUCTUS
    [[nodiscard]] VMSymbol* GetInstanceFieldName(size_t index) const;
#endif
    [[nodiscard]] size_t GetNumberOfInstanceFields() const;
#ifndef FRUCTUS
    [[nodiscard]] bool HasPrimitives() const;
    void LoadPrimitives(bool showWarning);
#endif
    [[nodiscard]] VMClass* CloneForMovingGC() const override;

    DECLARE_AS_DEBUG_STRING(override)

    /** The fields of every class: name, instanceFields and so on. */
    static const size_t VMClassNumberOfFields;

private:
    make_testable(public);

    // Remember to update Parser::superclass when the fields are changed
    // Theses are treated like VMObject fields and initialized that way
    GCSymbol* name;
#ifdef FRUCTUS
    // the image has no field names, so this is how many fields there are,
    // tagged, where the host has the array of their names
    gc_oop_t numberOfInstanceFields;
#else
    GCArray* instanceFields;
#endif
    GCArray* instanceInvokables;
    GCObject* superClass;
};
