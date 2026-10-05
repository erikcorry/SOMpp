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
#include <type_traits>

#include "../misc/defs.h"

/**
 * The type of SOM's small integers, and the type used for all arithmetic on
 * them. It is as wide as a pointer, because small integers are tagged
 * pointers, so on a 16-bit target a small integer has 15 bits.
 * Values that may be out of the small integer range, before they are checked,
 * still use int64_t, for instance at the boundary to big integers.
 */
typedef intptr_t som_int_t;
typedef uintptr_t som_uint_t;

#ifdef FRUCTUS
/**
 * The value of a boxed integer, a VMInteger. On fructus an integer that
 * doesn't fit in a small integer is boxed, with 64 bits, which wrap around:
 * there are no big integers, and no overflow.
 */
typedef int64_t som_box_int_t;
#else
typedef som_int_t som_box_int_t;
#endif

#ifdef FRUCTUS
/** A hash as a small integer: its low 15 bits, sign extended. */
inline som_int_t SmallIntHash(som_uint_t bits) {
    return (som_int_t)(bits << 1U) >> 1U;
}

/**
 * The hash of an object, which is its address: fructus' GC doesn't move
 * objects. Addresses are even, so the low bit is dropped.
 */
inline som_int_t AddressHash(const void* obj) {
    return SmallIntHash((som_uint_t)obj >> 1U);
}
#endif

constexpr int SOM_INT_BITS = sizeof(som_int_t) * 8;

// some MACROS for integer tagging

/**
 * max value for tagged integers
 * 01111111 11111111 ... 11111111 1111111X
 *
 */
#define VMTAGGEDINTEGER_MAX (INTPTR_MAX >> 1)

/**
 * min value for tagged integers
 * 10000000 00000000 ... 00000000 0000000X
 */
#define VMTAGGEDINTEGER_MIN (INTPTR_MIN >> 1)

/**
 * The comparison is done in the type of value, which may be wider than
 * som_int_t, for instance an int64_t parsed from a literal, or unsigned, for
 * instance a size_t.
 */
template <typename T>
__attribute__((always_inline)) constexpr bool
VMTAGGED_INTEGER_WITHIN_RANGE_CHECK(T value) {
    if constexpr (std::is_unsigned_v<T>) {
        return value <= (som_uint_t)VMTAGGEDINTEGER_MAX;
    } else {
        return value >= VMTAGGEDINTEGER_MIN && value <= VMTAGGEDINTEGER_MAX;
    }
}

#ifdef FRUCTUS
  // there are no big integers: a 64-bit VMInteger, see som_box_int_t
  #define TAG_INTEGER(X)                                 \
      ((VMTAGGED_INTEGER_WITHIN_RANGE_CHECK(X))          \
           ? ((vm_oop_t)((((uintptr_t)(X)) << 1U) | 1U)) \
           : BoxInteger(X))
#elif ADDITIONAL_ALLOCATION
  #define TAG_INTEGER(X)                                                   \
      ((VMTAGGED_INTEGER_WITHIN_RANGE_CHECK(X) && Universe::NewInteger(0)) \
           ? ((vm_oop_t)((((uintptr_t)(X)) << 1U) | 1U))                   \
           : (Universe::NewBigIntegerFromInt(X)))
#else
  #define TAG_INTEGER(X)                                 \
      ((VMTAGGED_INTEGER_WITHIN_RANGE_CHECK(X))          \
           ? ((vm_oop_t)((((uintptr_t)(X)) << 1U) | 1U)) \
           : (Universe::NewBigIntegerFromInt(X)))
#endif

#if USE_TAGGING
  #define SMALL_INT_VAL(X) \
      ((som_int_t)(X) >> 1U) /* NOLINT (hicpp-signed-bitwise) */
  #define NEW_INT(X) (TAG_INTEGER((X)))
  #define IS_TAGGED(X) ((bool)((uintptr_t)(X) & 1U))
  #define IS_SMALL_INT(X) IS_TAGGED(X)
  #ifdef FRUCTUS
    // there are no big integers and no doubles
    #define IS_BIG_INT(X) false
    #define IS_DOUBLE(X) false
  #else
    #define IS_BIG_INT(X) IsVMBigInteger(X)
    #define IS_DOUBLE(X) IsVMDouble(X)
  #endif
  #define AS_BIG_INT(X) (static_cast<VMBigInteger*>(X))
  #define AS_DOUBLE(X) (static_cast<VMDouble*>(X)->GetEmbeddedDouble())
  #define CLASS_OF(X)                        \
      (IS_TAGGED(X) ? load_ptr(integerClass) \
                    : ((AbstractVMObject*)(X))->GetClass())
  #define AS_OBJ(X) \
      (IS_TAGGED(X) ? GlobalBox::IntegerBox() : ((AbstractVMObject*)(X)))
#else
  #define SMALL_INT_VAL(X) (static_cast<VMInteger*>(X)->GetEmbeddedInteger())
  #define NEW_INT(X) (Universe::NewInteger(X))
  #define IS_TAGGED(X) false
  #define IS_SMALL_INT(X) IsVMInteger(X)
  #define IS_BIG_INT(X) IsVMBigInteger(X)
  #define AS_BIG_INT(X) (static_cast<VMBigInteger*>(X))
  #define IS_DOUBLE(X) IsVMDouble(X)
  #define AS_DOUBLE(X) (static_cast<VMDouble*>(X)->GetEmbeddedDouble())
  #define CLASS_OF(X) (AS_OBJ(X)->GetClass())
  #define AS_OBJ(X) ((AbstractVMObject*)(X))
#endif

// Forward definitions of VM object classes
class AbstractVMObject;
class VMArray;
class VMVector;
class VMBlock;
class VMClass;
class VMDouble;
class VMEvaluationPrimitive;
class VMFrame;
class VMBigInteger;
class VMInteger;
class VMInvokable;
class VMMethod;
class VMObject;
class VMPrimitive;
class VMSafePrimitive;
class VMSafeUnaryPrimitive;
class VMSafeBinaryPrimitive;
class VMSafeTernaryPrimitive;
class VMTrivialMethod;
class VMLiteralReturn;
class VMGlobalReturn;
class VMGetter;
class VMSetter;
class VMString;
class VMSymbol;

// VMOop and GCOop are classes to be able to type the pointer that can be
// tagged ints as well as AbstractVMObjects. Distinguish between stored
// pointers and loaded pointers, i.e., GCOop and VMOop respectively.
// Every GCAbstractObject can be directly stored as a GCOop.
// Integers need to be tagged first, but pointers don't.
// On the other hand, VMOops need to be untagged first to become useful.
class GCOop;
class VMOop {
    virtual void dummyVirtualFunctionToForceVTableCreation() {
        /* With the current class hierarchy, we need to force the compiler to
           create a VTable early, otherwise, the object layout is having
           vtables in the body of the objects, and casting is messed up,
           leading to offset pointers to the vtables of subclasses. */
    };

public:
    typedef GCOop Stored;
    virtual ~VMOop() = default;
};

class GCOop {
public:
    typedef VMOop Loaded;
};

// oop_t: Ordinary Object Pointer type
// an oop_t can refer to tagged integers as well as normal AbstractVMObjects
typedef VMOop* vm_oop_t;
typedef GCOop* gc_oop_t;

#ifdef FRUCTUS
/** A new VMInteger, for a value that isn't a small integer. */
vm_oop_t BoxInteger(som_box_int_t value);
#endif

/**
 We need to distinguish between pointers that need to be handled with a
 read barrier, and between pointers that already went through it.

 So, we call pointers that need to go through the barrier:
 heap values, or GC* pointers.

 And all the stuff that was already processed:
 loaded values, or VM* pointers.
 */
// clang-format off
class GCAbstractObject : public GCOop            { public: typedef AbstractVMObject Loaded; };
class GCObject         : public GCAbstractObject { public: typedef VMObject         Loaded; };
class GCFrame          : public GCAbstractObject { public: typedef VMFrame          Loaded; };
class GCClass          : public GCObject         { public: typedef VMClass          Loaded; };
class GCArray          : public GCObject         { public: typedef VMArray          Loaded; };
class GCVector         : public GCObject         { public: typedef VMVector         Loaded; };
class GCBlock          : public GCObject         { public: typedef VMBlock          Loaded; };
class GCDouble         : public GCAbstractObject { public: typedef VMDouble         Loaded; };
class GCInteger        : public GCAbstractObject { public: typedef VMInteger        Loaded; };
class GCBigInteger     : public GCAbstractObject { public: typedef VMBigInteger     Loaded; };
class GCInvokable      : public GCAbstractObject { public: typedef VMInvokable      Loaded; };
class GCMethod         : public GCInvokable      { public: typedef VMMethod         Loaded; };
class GCPrimitive      : public GCInvokable      { public: typedef VMPrimitive      Loaded; };
class GCEvaluationPrimitive : public GCInvokable { public: typedef VMEvaluationPrimitive Loaded; };
class GCSafePrimitive  : public GCInvokable      { public: typedef VMSafePrimitive  Loaded; };
class GCSafeUnaryPrimitive   : public GCSafePrimitive { public: typedef VMSafeUnaryPrimitive   Loaded; };
class GCSafeBinaryPrimitive  : public GCSafePrimitive { public: typedef VMSafeBinaryPrimitive  Loaded; };
class GCSafeTernaryPrimitive : public GCSafePrimitive { public: typedef VMSafeTernaryPrimitive Loaded; };
class GCTrivialMethod  : public GCInvokable      { public: typedef VMTrivialMethod  Loaded; };
class GCLiteralReturn  : public GCTrivialMethod  { public: typedef VMLiteralReturn  Loaded; };
class GCGlobalReturn   : public GCTrivialMethod  { public: typedef VMGlobalReturn   Loaded; };
class GCGetter         : public GCTrivialMethod  { public: typedef VMGetter         Loaded; };
class GCSetter         : public GCTrivialMethod  { public: typedef VMSetter         Loaded; };
class GCString         : public GCAbstractObject { public: typedef VMString         Loaded; };
class GCSymbol         : public GCString         { public: typedef VMSymbol         Loaded; };
// clang-format on

// Used to mark object fields as invalid
#define INVALID_VM_POINTER ((VMObject*)0x101010)
#define INVALID_GC_POINTER ((GCObject*)0x101010)

template <typename T>
inline typename T::Loaded* load_ptr(T* gc_val) {
    return (typename T::Loaded*)gc_val;
}

/** To store object into a root. */
template <typename T>
inline typename T::Stored* store_root(T* vm_val) {
    return (typename T::Stored*)vm_val;
}

/** For temporary use, but can't be stored, and can't be alive across GC
 * invocations. */
template <typename T>
inline typename T::Stored* tmp_ptr(T* vm_val) {
    return (typename T::Stored*)vm_val;
}

/** To store object a field, needs special care to correctly call
 * `write_barrier()` separately. */
template <typename T>
inline typename T::Stored* store_with_separate_barrier(T* vm_val) {
    return (typename T::Stored*)vm_val;
}

/** Standard assignment of pointer to field, including write barrier. */
#define store_ptr(field, val)                 \
    field = store_with_separate_barrier(val); \
    write_barrier(this, val)

typedef gc_oop_t (*walk_heap_fn)(gc_oop_t);
