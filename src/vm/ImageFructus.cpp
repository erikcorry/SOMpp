/*
 * The fructus side of the image, see FructusImage.h: the objects the host
 * wrote are linked in, and here they are registered with the VM.
 */

#include <stddef.h>

#include "../interpreter/SendSite.h"
#include "../memory/Heap.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMArray.h"
#include "../vmobjects/VMClass.h"
#include "../vmobjects/VMEvaluationPrimitive.h"
#include "../vmobjects/VMMethod.h"
#include "../vmobjects/VMObject.h"
#include "../vmobjects/VMPrimitive.h"
#include "../vmobjects/VMSafePrimitive.h"
#include "../vmobjects/VMString.h"
#include "../vmobjects/VMSymbol.h"
#include "../vmobjects/VMTrivialMethod.h"
#include "FructusImage.h"
#include "Globals.h"

namespace {
/** An entry of the som_globals section. */
struct ImageGlobal {
    GCSymbol* name;
    gc_oop_t value;
};
}  // namespace

// written by the host, see FructusImage.h
extern "C" {
extern VMObject som_nil;
extern VMObject som_true;
extern VMObject som_false;
extern VMObject som_system;
extern VMArray som_arguments;
extern GCClass* const som_main_class;
extern GCSymbol* const som_main_selector;
extern GCClass* const som_block_classes[maxBlockClassArguments + 1];

extern VMClass som_class_Object;
extern VMClass som_class_Class;
extern VMClass som_class_Metaclass;
extern VMClass som_class_Nil;
extern VMClass som_class_Array;
extern VMClass som_class_Vector;
extern VMClass som_class_Method;
extern VMClass som_class_Symbol;
extern VMClass som_class_Integer;
extern VMClass som_class_Primitive;
extern VMClass som_class_String;
extern VMClass som_class_Block;
extern VMClass som_class_True;
extern VMClass som_class_False;
extern VMClass som_class_System;

// the globals of all classes, which the linker gathers
extern ImageGlobal __start_som_globals[];
extern ImageGlobal __stop_som_globals[];
}

void FructusImage::Load() {
    nilObject = store_root(&som_nil);
    trueObject = store_root(&som_true);
    falseObject = store_root(&som_false);

    objectClass = store_root(&som_class_Object);
    classClass = store_root(&som_class_Class);
    metaClassClass = store_root(&som_class_Metaclass);
    nilClass = store_root(&som_class_Nil);
    arrayClass = store_root(&som_class_Array);
    vectorClass = store_root(&som_class_Vector);
    methodClass = store_root(&som_class_Method);
    symbolClass = store_root(&som_class_Symbol);
    integerClass = store_root(&som_class_Integer);
    primitiveClass = store_root(&som_class_Primitive);
    stringClass = store_root(&som_class_String);
    blockClass = store_root(&som_class_Block);
    trueClass = store_root(&som_class_True);
    falseClass = store_root(&som_class_False);
    systemClass = store_root(&som_class_System);
}

VMObject* FructusImage::SystemObject() {
    return &som_system;
}

VMClass* FructusImage::MainClass() {
    return load_ptr(som_main_class);
}

VMArray* FructusImage::Arguments() {
    return &som_arguments;
}

VMSymbol* FructusImage::MainSelector() {
    return load_ptr(som_main_selector);
}

VMClass* FructusImage::BlockClass(uint8_t numberOfArguments) {
    if (numberOfArguments > maxBlockClassArguments) {
        return nullptr;
    }
    GCClass* cls = som_block_classes[numberOfArguments];
    return cls == nullptr ? nullptr : load_ptr(cls);
}

vm_oop_t FructusImage::GetGlobal(VMSymbol* name) {
    for (const ImageGlobal* g = __start_som_globals; g < __stop_som_globals;
         g++) {
        if (load_ptr(g->name) == name) {
            return load_ptr(g->value);
        }
    }
    return nullptr;
}

void FructusImage::WalkGlobals(walk_heap_fn walk) {
    // the GC doesn't move objects, so the globals stay as they are
    for (const ImageGlobal* g = __start_som_globals; g < __stop_som_globals;
         g++) {
        walk(g->value);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#define CHECK_SIZE(cls, size) \
    static_assert(sizeof(cls) == (size), "size of " #cls)
#define CHECK_FIELD(cls, field, offset)                       \
    static_assert(offsetof(cls, field) == FructusLayout::offset, \
                  "offset of " #cls "::" #field)

/** The layout the host writes the image with is the one fructus uses. */
void FructusImage::CheckLayout() {
    namespace L = FructusLayout;

    static_assert(sizeof(void*) == L::word, "word size");
    static_assert(PADDED_SIZE(1) == L::PaddedSize(1), "padding");
    static_assert(VMTAGGEDINTEGER_MIN == L::smallIntMin, "small integers");
    static_assert(VMTAGGEDINTEGER_MAX == L::smallIntMax, "small integers");

    CHECK_FIELD(VMObject, gcField, gcField);
    CHECK_FIELD(VMObject, totalObjectSize, VMObject_totalObjectSize);
    CHECK_FIELD(VMObject, numberOfFields, VMObject_numberOfFields);
    CHECK_FIELD(VMObject, clazz, VMObject_clazz);
    CHECK_SIZE(VMObject, L::VMObject_size);

    CHECK_FIELD(VMClass, name, VMClass_name);
    CHECK_FIELD(VMClass, instanceFields, VMClass_instanceFields);
    CHECK_FIELD(VMClass, instanceInvokables, VMClass_instanceInvokables);
    CHECK_FIELD(VMClass, superClass, VMClass_superClass);
    CHECK_SIZE(VMClass, L::VMClass_size);

    CHECK_SIZE(VMArray, L::VMArray_size);

    CHECK_FIELD(VMString, gcField, gcField);
    CHECK_FIELD(VMString, length, VMString_length);
    CHECK_FIELD(VMString, chars, VMString_chars);
    CHECK_SIZE(VMString, L::VMString_size);

    CHECK_FIELD(VMSymbol, numberOfArgumentsOfSignature,
                VMSymbol_numberOfArgumentsOfSignature);
    CHECK_FIELD(VMSymbol, length, VMSymbol_length);
    CHECK_SIZE(VMSymbol, L::VMSymbol_size);

    static_assert(SendSite::numberOfArguments == L::SendSite_numberOfArguments);
    static_assert(SendSite::target == L::SendSite_target);
    static_assert(SendSite::receiverClass == L::SendSite_receiverClass);
    static_assert(SendSite::length == L::SendSite_length);

    CHECK_FIELD(VMInvokable, gcField, gcField);
    CHECK_FIELD(VMInvokable, signature, VMInvokable_signature);
    CHECK_FIELD(VMInvokable, holder, VMInvokable_holder);

    CHECK_FIELD(VMMethod, numberOfLocals, VMMethod_numberOfLocals);
    CHECK_FIELD(VMMethod, maximumNumberOfStackElements,
                VMMethod_maximumNumberOfStackElements);
    CHECK_FIELD(VMMethod, bcLength, VMMethod_bcLength);
    CHECK_FIELD(VMMethod, numberOfArguments, VMMethod_numberOfArguments);
    CHECK_FIELD(VMMethod, numberOfConstants, VMMethod_numberOfConstants);
    CHECK_FIELD(VMMethod, indexableFields, VMMethod_indexableFields);
    CHECK_FIELD(VMMethod, bytecodes, VMMethod_bytecodes);
    CHECK_SIZE(VMMethod, L::VMMethod_size);

    CHECK_FIELD(VMLiteralReturn, literal, VMLiteralReturn_literal);
    CHECK_FIELD(VMLiteralReturn, numberOfArguments,
                VMLiteralReturn_numberOfArguments);
    CHECK_SIZE(VMLiteralReturn, L::VMLiteralReturn_size);

    CHECK_FIELD(VMGlobalReturn, globalName, VMGlobalReturn_globalName);
    CHECK_FIELD(VMGlobalReturn, numberOfArguments,
                VMGlobalReturn_numberOfArguments);
    CHECK_SIZE(VMGlobalReturn, L::VMGlobalReturn_size);

    CHECK_FIELD(VMGetter, fieldIndex, VMGetter_fieldIndex);
    CHECK_FIELD(VMGetter, numberOfArguments, VMGetter_numberOfArguments);
    CHECK_SIZE(VMGetter, L::VMGetter_size);

    CHECK_FIELD(VMSetter, fieldIndex, VMSetter_fieldIndex);
    CHECK_FIELD(VMSetter, argIndex, VMSetter_argIndex);
    CHECK_FIELD(VMSetter, numberOfArguments, VMSetter_numberOfArguments);
    CHECK_SIZE(VMSetter, L::VMSetter_size);

    CHECK_FIELD(VMPrimitive, prim, VMPrimitive_prim);
    CHECK_SIZE(VMPrimitive, L::VMPrimitive_size);
    CHECK_FIELD(VMSafeUnaryPrimitive, prim, VMPrimitive_prim);
    CHECK_SIZE(VMSafeUnaryPrimitive, L::VMPrimitive_size);
    CHECK_FIELD(VMSafeBinaryPrimitive, prim, VMPrimitive_prim);
    CHECK_SIZE(VMSafeBinaryPrimitive, L::VMPrimitive_size);
    CHECK_FIELD(VMSafeTernaryPrimitive, prim, VMPrimitive_prim);
    CHECK_SIZE(VMSafeTernaryPrimitive, L::VMPrimitive_size);
    static_assert(offsetof(FramePrim, pointer) == 0, "a Prim is its routine");
    static_assert(offsetof(UnaryPrim, pointer) == 0, "a Prim is its routine");
    static_assert(offsetof(BinaryPrim, pointer) == 0, "a Prim is its routine");
    static_assert(offsetof(TernaryPrim, pointer) == 0,
                  "a Prim is its routine");

    CHECK_FIELD(VMEvaluationPrimitive, numberOfArguments,
                VMEvaluationPrimitive_numberOfArguments);
    CHECK_SIZE(VMEvaluationPrimitive, L::VMEvaluationPrimitive_size);

    CHECK_SIZE(ImageGlobal, 2 * L::word);
}

#pragma GCC diagnostic pop
