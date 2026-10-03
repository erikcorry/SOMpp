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

#include "PrimitiveContainer.h"

#include <assert.h>
#include <cstddef>

#include "../vm/Print.h"
#include "../vm/Symbols.h"
#include "../vm/Universe.h"
#include "../vmobjects/VMClass.h"
#include "../vmobjects/VMInvokable.h"
#include "../vmobjects/VMPrimitive.h"
#include "../vmobjects/VMSafePrimitive.h"
#include "../vmobjects/VMSymbol.h"
#include "Primitives.h"

void PrimitiveContainer::Add(const char* name, FramePrimitiveRoutine routine,
                             bool classSide, size_t hash) {
    framePrims.push_back({name, FramePrim(routine, classSide, hash), FramePrim()});
}

void PrimitiveContainer::Add(const char* name, BinaryPrimitiveRoutine routine,
                             bool classSide, size_t hash) {
    binaryPrims.push_back({name, BinaryPrim(routine, classSide, hash), BinaryPrim()});
}

void PrimitiveContainer::Add(const char* name, UnaryPrimitiveRoutine routine,
                             bool classSide, size_t hash) {
    unaryPrims.push_back({name, UnaryPrim(routine, classSide, hash), UnaryPrim()});
}

void PrimitiveContainer::Add(const char* name, TernaryPrimitiveRoutine routine,
                             bool classSide, size_t hash) {
    ternaryPrims.push_back({name, TernaryPrim(routine, classSide, hash), TernaryPrim()});
}

void PrimitiveContainer::Add(const char* name, bool classSide,
                             FramePrimitiveRoutine routine1, size_t hash1,
                             FramePrimitiveRoutine routine2, size_t hash2) {
    framePrims.push_back({name, FramePrim(routine1, classSide, hash1), FramePrim(routine2, classSide, hash2)});
}

void PrimitiveContainer::Add(const char* name, bool classSide,
                             BinaryPrimitiveRoutine routine1, size_t hash1,
                             BinaryPrimitiveRoutine routine2, size_t hash2) {
    binaryPrims.push_back({name, BinaryPrim(routine1, classSide, hash1), BinaryPrim(routine2, classSide, hash2)});
}

void PrimitiveContainer::Add(const char* name, bool classSide,
                             UnaryPrimitiveRoutine routine1, size_t hash1,
                             UnaryPrimitiveRoutine routine2, size_t hash2) {
    unaryPrims.push_back({name, UnaryPrim(routine1, classSide, hash1), UnaryPrim(routine2, classSide, hash2)});
}

void PrimitiveContainer::Add(const char* name, bool classSide,
                             TernaryPrimitiveRoutine routine1, size_t hash1,
                             TernaryPrimitiveRoutine routine2, size_t hash2) {
    ternaryPrims.push_back({name, TernaryPrim(routine1, classSide, hash1), TernaryPrim(routine2, classSide, hash2)});
}

template <class PrimT>
bool PrimitiveContainer::installPrimitives(
    bool classSide, bool showWarning, VMClass* clazz,
    SimpleVector<Entry<PrimT>>& prims,
    VMInvokable* (*makePrimFn)(VMSymbol* sig, PrimT)) {
    bool hasHashMismatch = false;

    for (auto const& p : prims) {
        PrimT prim1 = p.prim1;
        PrimT prim2 = p.prim2;
        assert(prim1.IsValid());

        if (classSide != prim1.isClassSide) {
            continue;
        }

        VMSymbol* sig = SymbolFor(p.name);
        PrimInstallResult const result = clazz->InstallPrimitive(
            makePrimFn(sig, prim1), prim1.bytecodeHash, !prim2.IsValid());

        if (!prim2.IsValid()) {
            hasHashMismatch =
                hasHashMismatch || result == PrimInstallResult::HASH_MISMATCH;
        }

        if (result == PrimInstallResult::INSTALLED_ADDED && showWarning) {
            VMSymbol const* className = clazz->GetName();
            ErrorPrintf("Warn: Primitive %s is not in class definition for "
                        "class %.*s\n",
                        p.name, (int)className->GetStringLength(),
                        className->GetRawChars());
        } else if (result == PrimInstallResult::HASH_MISMATCH &&
                   prim2.IsValid()) {
            assert(prim1.isClassSide == prim2.isClassSide);
            PrimInstallResult const result2 = clazz->InstallPrimitive(
                makePrimFn(sig, prim2), prim2.bytecodeHash, true);
            hasHashMismatch =
                hasHashMismatch || result2 == PrimInstallResult::HASH_MISMATCH;
        }
    }
    return hasHashMismatch;
}

void PrimitiveContainer::InstallPrimitives(VMClass* clazz, bool classSide,
                                           bool showWarning) {
    bool hasHashMismatch = false;
    hasHashMismatch =
        hasHashMismatch ||
        installPrimitives(classSide, showWarning, clazz, unaryPrims,
                          VMSafePrimitive::GetSafeUnary);
    hasHashMismatch =
        hasHashMismatch ||
        installPrimitives(classSide, showWarning, clazz, binaryPrims,
                          VMSafePrimitive::GetSafeBinary);
    hasHashMismatch =
        hasHashMismatch ||
        installPrimitives(classSide, showWarning, clazz, ternaryPrims,
                          VMSafePrimitive::GetSafeTernary);
    hasHashMismatch = hasHashMismatch ||
                      installPrimitives(classSide, showWarning, clazz,
                                        framePrims, VMPrimitive::GetFramePrim);

    if (abortOnCoreLibHashMismatch && hasHashMismatch) {
        VMSymbol const* className = clazz->GetName();
        ErrorPrintf("The implementation of methods in %.*s seem to have "
                    "changed.\n",
                    (int)className->GetStringLength(),
                    className->GetRawChars());
        ErrorPrint(
            "The primitive implementation in the matching VM class may need to "
            "be changed. See for instance _Vector::_Vector() in "
            "primitives/Vector.cpp\n");
        Quit(1);
    }
}
