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

#include "VMClass.h"

#include <assert.h>
#include <cstdint>
#include <string.h>

#include "../memory/Heap.h"
#include "../misc/defs.h"
#ifndef FRUCTUS
  #include "../primitivesCore/PrimitiveLoader.h"
#endif
#include "../vm/Globals.h"
#include "../vm/IsValidObject.h"
#include "../vm/Print.h"
#include "ObjectFormats.h"
#include "VMArray.h"
#include "VMInvokable.h"
#include "VMMethod.h"
#include "VMObject.h"
#include "VMSymbol.h"

#ifdef FRUCTUS
// no instanceInvokables, see VMClass.h
const size_t VMClass::VMClassNumberOfFields = 3;
#else
const size_t VMClass::VMClassNumberOfFields = 4;
#endif

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init, hicpp-member-init)
VMClass::VMClass() : VMObject(VMClassNumberOfFields, sizeof(VMClass)) {}

VMClass* VMClass::CloneForMovingGC() const {
    auto* clone =
        new (GetHeap<HEAP_CLS>(),
             totalObjectSize - sizeof(VMClass) ALLOC_MATURE) VMClass(*this);
    memcpy(SHIFTED_PTR(clone, sizeof(VMObject)),
           SHIFTED_PTR(this, sizeof(VMObject)),
           GetObjectSize() - sizeof(VMObject));
    return clone;
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init, hicpp-member-init)
VMClass::VMClass(size_t numberOfFields, size_t additionalBytes)
    : VMObject(numberOfFields + VMClassNumberOfFields,
               additionalBytes + sizeof(VMClass)) {}

#ifndef FRUCTUS
PrimInstallResult VMClass::InstallPrimitive(VMInvokable* invokable,
                                            size_t bytecodeHash,
                                            bool reportHashMismatch) {
    if (invokable == nullptr) {
        ErrorExit("Error: trying to add non-invokable to invokables array");
        return PrimInstallResult::NULL_ARG;
    }

    // Check whether an invokable with the same signature exists and replace it
    // if that's the case
    VMArray const* instInvokables = load_ptr(instanceInvokables);
    size_t const numIndexableFields =
        instInvokables->GetNumberOfIndexableFields();

    for (size_t i = 0; i < numIndexableFields; ++i) {
        auto* inv =
            static_cast<VMInvokable*>(instInvokables->GetIndexableField(i));

        if (inv == nullptr) {
            ErrorExit(
                "Invokables array corrupted. "
                "Either NULL pointer added or pointer to non-invokable.");
            return PrimInstallResult::NULL_IN_INVOKABLES;
        }

        if (invokable->GetSignature() == inv->GetSignature()) {
            PrimInstallResult result = PrimInstallResult::NULL_ARG;
            bool hashMismatch = false;
            size_t seenHash = 0;

            if (bytecodeHash != 0) {
                if (!IsVMMethod((vm_oop_t)inv)) {
                    assert(inv->GetHolder() != nullptr);
                    ErrorPrintf(
                        "Expected a bytecode method, but found something else "
                        "for %.*s>>#%.*s",
                        STR_ARGS(inv->GetHolder()->GetName()),
                        STR_ARGS(inv->GetSignature()));
                    result = PrimInstallResult::HASH_MISMATCH;
                    hashMismatch = true;
                } else {
                    auto* method = static_cast<VMMethod*>(inv);
                    seenHash = method->GetBytecodeHash();
                    if (seenHash == bytecodeHash) {
                        result = PrimInstallResult::INSTALLED_REPLACED;
                    } else {
                        result = PrimInstallResult::HASH_MISMATCH;
                        hashMismatch = true;
                    }
                }
            } else {
                result = PrimInstallResult::INSTALLED_REPLACED;
            }

            if (!hashMismatch) {
                SetInstanceInvokable(i, invokable);
            } else if (reportHashMismatch) {
                ErrorPrintf(
                    "Warn: Primitive %.*s>>#%.*s was not installed.\n"
                    "Warn: Bytecode hash did not match.\n"
                    "Warn: expected hash: %zu\n"
                    "Warn: actual hash:   %zu\n",
                    STR_ARGS(inv->GetHolder()->GetName()),
                    STR_ARGS(invokable->GetSignature()), bytecodeHash,
                    seenHash);
            }
            return result;
        }
    }
    // it's a new invokable so we need to expand the invokables array.
    store_ptr(instanceInvokables,
              instInvokables->CopyAndExtendWith((vm_oop_t)invokable));

    // set holder, since we don't call SetInstanceInvokable, which does it
    // NOLINTNEXTLINE (cppcoreguidelines-pro-type-reinterpret-cast)
    if (invokable != reinterpret_cast<VMInvokable*>(load_ptr(nilObject))) {
        invokable->SetHolder(this);
    }
    return PrimInstallResult::INSTALLED_ADDED;
}
#endif

#ifndef FRUCTUS
// fructus has no reflection on field names
VMSymbol* VMClass::GetInstanceFieldName(size_t index) const {
    return static_cast<VMSymbol*>(
        load_ptr(instanceFields)->GetIndexableField(index));
}
#endif

#ifndef FRUCTUS
// the image has the classes on fructus, whose invokables have no holder
void VMClass::SetInstanceInvokables(VMArray* invokables) {
    store_ptr(instanceInvokables, invokables);
    vm_oop_t nil = load_ptr(nilObject);

    size_t const numInvokables = GetNumberOfInstanceInvokables();
    for (size_t i = 0; i < numInvokables; ++i) {
        vm_oop_t invo = load_ptr(instanceInvokables)->GetIndexableField(i);
        // check for Nil object
        if (invo != nil) {
            // not Nil, so this actually is an invokable
            auto* inv = (VMInvokable*)invo;
            inv->SetHolder(this);
        }
    }
}
#endif

#ifndef FRUCTUS
// fructus finds the invokables by their classIds, see VMInvokable::IsMethodOf
size_t VMClass::GetNumberOfInstanceInvokables() const {
    return load_ptr(instanceInvokables)->GetNumberOfIndexableFields();
}

VMInvokable* VMClass::GetInstanceInvokable(size_t index) const {
    return static_cast<VMInvokable*>(
        load_ptr(instanceInvokables)->GetIndexableField(index));
}

void VMClass::SetInstanceInvokable(size_t index, VMInvokable* invokable) {
    load_ptr(instanceInvokables)->SetIndexableField(index, invokable);

    // NOLINTNEXTLINE (cppcoreguidelines-pro-type-reinterpret-cast)
    if (invokable != reinterpret_cast<VMInvokable*>(load_ptr(nilObject))) {
        invokable->SetHolder(this);
    }
}

VMInvokable* VMClass::LookupInvokable(VMSymbol* name) {
    assert(IsValidObject(this));

    // the sends cache what they find, see SendSite.h
    size_t const numInvokables = GetNumberOfInstanceInvokables();
    for (size_t i = 0; i < numInvokables; ++i) {
        VMInvokable* invokable = GetInstanceInvokable(i);
        if (invokable->GetSignature() == name) {
            return invokable;
        }
    }

    // look in super class
    if (HasSuperClass()) {
        return ((VMClass*)load_ptr(superClass))->LookupInvokable(name);
    }

    // invokable not found
    return nullptr;
}
#endif

#ifndef FRUCTUS
// fructus has no reflection on field names
som_int_t VMClass::LookupFieldIndex(VMSymbol* name) const {
    size_t const numInstanceFields = GetNumberOfInstanceFields();
    for (size_t i = 0; i < numInstanceFields; ++i) {
        if (name == GetInstanceFieldName(i)) {
            return (som_int_t)i;
        }
    }
    return -1;
}
#endif

// the instance fields include those of the superclasses, see
// ClassGenerationContext::SetInstanceFieldsOfSuper()
size_t VMClass::GetNumberOfInstanceFields() const {
#ifdef FRUCTUS
    return SMALL_INT_VAL(load_ptr(numberOfInstanceFields));
#else
    return load_ptr(instanceFields)->GetNumberOfIndexableFields();
#endif
}

#ifndef FRUCTUS
bool VMClass::HasPrimitives() const {
    size_t const numInvokables = GetNumberOfInstanceInvokables();
    for (size_t i = 0; i < numInvokables; ++i) {
        VMInvokable const* const invokable = GetInstanceInvokable(i);
        if (invokable->IsPrimitive()) {
            return true;
        }
    }
    return false;
}

void VMClass::LoadPrimitives(bool showWarning) {
    VMSymbol const* const cname = load_ptr(name);
    const char* const chars = cname->GetRawChars();
    size_t const length = cname->GetStringLength();

    PrimitiveLoader::InstallPrimitives(chars, length, this, false,
                                       showWarning);
    PrimitiveLoader::InstallPrimitives(chars, length, GetClass(), true,
                                       showWarning);
}
#endif

#if DEBUG
std::string VMClass::AsDebugString() const {
    return "Class(" + GetName()->GetStdString() + ")";
}
#endif

VMObject* VMClass::GetSuperClass() const {
    return load_ptr(superClass);
}

void VMClass::SetSuperClass(VMObject* sup) {
    store_ptr(superClass, sup);
}

#ifndef FRUCTUS
VMSymbol* VMClass::GetName() const {
    return load_ptr(name);
}

void VMClass::SetName(VMSymbol* nam) {
    store_ptr(name, nam);
}
#endif

bool VMClass::HasSuperClass() const {
    assert(IsValidObject(load_ptr(superClass)));
    return load_ptr(superClass) != load_ptr(nilObject);
}

#ifndef FRUCTUS
VMArray* VMClass::GetInstanceFields() const {
    return load_ptr(instanceFields);
}

void VMClass::SetInstanceFields(VMArray* instFields) {
    store_ptr(instanceFields, instFields);
}
#endif

#ifndef FRUCTUS
VMArray* VMClass::GetInstanceInvokables() const {
    return load_ptr(instanceInvokables);
}
#endif
