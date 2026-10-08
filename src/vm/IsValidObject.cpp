#include "IsValidObject.h"

#include <assert.h>
#ifndef FRUCTUS
#include <new>
#endif

#ifndef FRUCTUS
#include "../compiler/Variable.h"
#endif
#include "../memory/Heap.h"
#include "../misc/defs.h"
#include "../vmobjects/AbstractObject.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMArray.h"
#ifndef FRUCTUS
#include "../vmobjects/VMBigInteger.h"
#endif
#include "../vmobjects/VMBlock.h"
#include "../vmobjects/VMClass.h"  // NOLINT(misc-include-cleaner) it's required to make the types complete
#include "../vmobjects/VMDouble.h"
#include "../vmobjects/VMEvaluationPrimitive.h"
#include "../vmobjects/VMFrame.h"
#include "../vmobjects/VMInteger.h"
#include "../vmobjects/VMMethod.h"
#include "../vmobjects/VMObjectBase.h"  // NOLINT(misc-include-cleaner) needed for some GCs
#include "../vmobjects/VMSafePrimitive.h"
#include "../vmobjects/VMString.h"
#include "../vmobjects/VMSymbol.h"
#include "../vmobjects/VMTrivialMethod.h"
#include "../vmobjects/VMVector.h"
#include "Globals.h"

static void* vt_array;
static void* vt_vector;
static void* vt_block;
static void* vt_class;
static void* vt_double;
static void* vt_eval_primitive;
static void* vt_frame;
static void* vt_integer;
static void* vt_big_integer;
static void* vt_method;
static void* vt_object;
static void* vt_primitive;
static void* vt_safe_un_primitive;
static void* vt_safe_bin_primitive;
static void* vt_safe_ter_primitive;
static void* vt_literal_return;
static void* vt_global_return;
static void* vt_getter;
static void* vt_setter;
static void* vt_string;
static void* vt_symbol;

bool IsValidObject(vm_oop_t obj) {
    if (!DEBUG) {
        return true;
    }

    if (obj == nullptr) {
        return true;
    }

    if (IS_TAGGED(obj)) {
        return true;
    }

    if (vt_symbol == nullptr) {  // initialization not yet completed
        return true;
    }

    if (obj == INVALID_VM_POINTER) {
        assert(obj == INVALID_VM_POINTER &&
               "Expected pointer to not be marker for invalid pointers.");
        return false;
    }

    void const* vt = *(void**)obj;
    bool b = vt == vt_array || vt == vt_block || vt == vt_class ||
             vt == vt_double || vt == vt_eval_primitive || vt == vt_frame ||
             vt == vt_integer || vt == vt_big_integer || vt == vt_method ||
             vt == vt_object || vt == vt_primitive ||
             vt == vt_safe_un_primitive || vt == vt_safe_bin_primitive ||
             vt == vt_safe_ter_primitive || vt == vt_string ||
             vt == vt_symbol || vt == vt_literal_return ||
             vt == vt_global_return || vt == vt_getter || vt == vt_setter ||
             vt == vt_vector;
    if (!b) {
        assert(b && "Expected vtable to be one of the known ones.");
        return false;
    }

#if GC_TYPE == COPYING || GC_TYPE == DEBUG_COPYING
    if (AS_OBJ(obj)->GetGCField() != 0) {
        // this is a properly forwarded object
        return true;
    }
#elif GC_TYPE == GENERATIONAL
    if ((AS_OBJ(obj)->GetGCField() & MASK_OBJECT_IS_OLD) == 0U) {
        if (AS_OBJ(obj)->GetGCField() != 0) {
            // this is a properly forwarded object
            return true;
        }
    }
#endif

    b = !AS_OBJ(obj)->IsMarkedInvalid();
    assert(b && "Expected object not to be marked as invalid.");
    return b;
}

void set_vt_to_null() {
    vt_array = nullptr;
    vt_vector = nullptr;
    vt_block = nullptr;
    vt_class = nullptr;
    vt_double = nullptr;
    vt_eval_primitive = nullptr;
    vt_frame = nullptr;
    vt_integer = nullptr;
    vt_big_integer = nullptr;
    vt_method = nullptr;
    vt_object = nullptr;
    vt_primitive = nullptr;
    vt_safe_un_primitive = nullptr;
    vt_safe_bin_primitive = nullptr;
    vt_safe_ter_primitive = nullptr;
    vt_literal_return = nullptr;
    vt_global_return = nullptr;
    vt_getter = nullptr;
    vt_setter = nullptr;
    vt_string = nullptr;
    vt_symbol = nullptr;
}

static void* get_vtable(AbstractVMObject* obj) {
    return *(void**)obj;
}

bool IsVMInteger(vm_oop_t obj) {
    assert(vt_integer != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_integer;
}

#ifndef FRUCTUS
bool IsVMDouble(vm_oop_t obj) {
    assert(vt_double != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_double;
}

bool IsVMBigInteger(vm_oop_t obj) {
    assert(vt_big_integer != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_big_integer;
}
#endif

bool IsVMMethod(vm_oop_t obj) {
    assert(vt_method != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_method;
}

bool IsVMInvokable(vm_oop_t obj) {
    if (IS_TAGGED(obj)) {
        return false;
    }
    void* vt = get_vtable(AS_OBJ(obj));
    return vt == vt_method || vt == vt_primitive ||
           vt == vt_safe_un_primitive || vt == vt_safe_bin_primitive ||
           vt == vt_safe_ter_primitive || vt == vt_eval_primitive ||
           vt == vt_literal_return || vt == vt_global_return ||
           vt == vt_getter || vt == vt_setter;
}

bool IsVMSymbol(vm_oop_t obj) {
    assert(vt_symbol != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_symbol;
}

bool IsLiteralReturn(vm_oop_t obj) {
    assert(vt_literal_return != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_literal_return;
}

bool IsGlobalReturn(vm_oop_t obj) {
    assert(vt_global_return != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_global_return;
}

bool IsGetter(vm_oop_t obj) {
    assert(vt_getter != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_getter;
}

bool IsSetter(vm_oop_t obj) {
    assert(vt_setter != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_setter;
}

bool IsSafeUnaryPrim(vm_oop_t obj) {
    assert(vt_safe_un_primitive != nullptr);
    return get_vtable(AS_OBJ(obj)) == vt_safe_un_primitive;
}

#ifndef FRUCTUS
// Answers the vtable of T, read from an object made on the stack and destroyed
// again. The collector never sees it, so it needn't be a valid SOM object.
template <class T, class... Args>
static void* vtable_of(Args... args) {
    alignas(T) char storage[sizeof(T)];
    T* const probe = ::new (storage) T(args...);
    void* const vtable = get_vtable(probe);
    probe->~T();
    return vtable;
}

void obtain_vtables_of_known_classes(VMSymbol* someValidSymbol) {
    // The objects are made outside the heap. The constructors' write barriers
    // do nothing, since the objects aren't old and IsValidObject() accepts
    // anything until vt_symbol is set.
    vt_array = vtable_of<VMArray>(0, 0);
    vt_vector = vtable_of<VMVector>(nullptr, nullptr, nullptr);
    vt_block = vtable_of<VMBlock>(nullptr, nullptr);
    vt_class = get_vtable(load_ptr(symbolClass));
    vt_double = vtable_of<VMDouble>(0.0);
    vt_eval_primitive = vtable_of<VMEvaluationPrimitive>(1);
    vt_integer = vtable_of<VMInteger>(0);
    vt_big_integer = vtable_of<VMBigInteger>("4611686018427387904", false);
    vt_object = get_vtable(load_ptr(nilObject));

    // the frame needs its method while it's made
    alignas(VMMethod) char methodStorage[sizeof(VMMethod)];
    auto* const mth =
        ::new (methodStorage) VMMethod(nullptr, 0, 0, 0, nullptr, nullptr);
    vt_method = get_vtable(mth);
    vt_frame = vtable_of<VMFrame>(0, mth, nullptr);
    mth->~VMMethod();

    vt_primitive = vtable_of<VMPrimitive>(someValidSymbol, FramePrim());
    vt_safe_un_primitive =
        vtable_of<VMSafeUnaryPrimitive>(someValidSymbol, UnaryPrim());
    vt_safe_bin_primitive =
        vtable_of<VMSafeBinaryPrimitive>(someValidSymbol, BinaryPrim());
    vt_safe_ter_primitive =
        vtable_of<VMSafeTernaryPrimitive>(someValidSymbol, TernaryPrim());

    vector<Variable> v;
    vt_literal_return =
        vtable_of<VMLiteralReturn>(someValidSymbol, v, someValidSymbol);
    vt_global_return =
        vtable_of<VMGlobalReturn>(someValidSymbol, v, someValidSymbol);
    vt_getter = vtable_of<VMGetter>(someValidSymbol, v, 0);
    vt_setter = vtable_of<VMSetter>(someValidSymbol, v, 0, 0);

    vt_string = vtable_of<VMString>(0, nullptr);
    vt_symbol = get_vtable(someValidSymbol);
}
#endif
