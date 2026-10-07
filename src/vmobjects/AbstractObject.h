#pragma once

/*
 * AbstractVMObject.h
 *
 *  Created on: 10.03.2011
 *      Author: christian
 */
#include <assert.h>
#include <cstddef>

#ifndef FRUCTUS
#include "../memory/CopyingHeap.h"
#include "../memory/DebugCopyingHeap.h"
#include "../memory/DebugMarkSweepHeap.h"
#include "../memory/GenerationalHeap.h"
#endif
#include "../memory/MarkSweepHeap.h"
#ifndef FRUCTUS
#include "../memory/PagedMarkSweepHeap.h"
#endif
#include "../misc/defs.h"
#include "../vm/Print.h"
#include "ObjectFormats.h"
#include "VMObjectBase.h"

using namespace std;

// this is the base class for all VMObjects
class AbstractVMObject : public VMObjectBase {
public:
    typedef GCAbstractObject Stored;

    [[nodiscard]] virtual som_int_t GetHash() const = 0;
    [[nodiscard]] virtual VMClass* GetClass() const = 0;
    [[nodiscard]] virtual AbstractVMObject* CloneForMovingGC() const = 0;
    /**
     * Sends selector from the interpreter's frame, with the receiver and
     * arguments pushed onto its stack. Answers false, and pops them again,
     * if the invokable couldn't allocate what it needed: the caller must then
     * undo what it did too, so the interpreter can run its bytecode again.
     */
    [[nodiscard]] bool Send(VMSymbol* selector, vm_oop_t* arguments,
                            size_t argc);

    /** Size in bytes of the object. */
    [[nodiscard]] virtual size_t GetObjectSize() const = 0;

    virtual void MarkObjectAsInvalid() = 0;
    [[nodiscard]] virtual bool IsMarkedInvalid() const = 0;

#if DEBUG
    [[nodiscard]] virtual std::string AsDebugString() const = 0;
#endif

    AbstractVMObject() = default;
    ~AbstractVMObject() override = default;

    [[nodiscard]] inline virtual size_t GetNumberOfFields() const {
        ErrorPrint("this object doesn't support GetNumberOfFields\n");
        return -1;
    }

    inline virtual void SetClass(VMClass* /*unused*/) {
        ErrorPrint("this object doesn't support SetClass\n");
    }

#ifndef FRUCTUS
    // fructus has no reflection on field names
    som_int_t GetFieldIndex(VMSymbol* fieldName) const;
#endif

    virtual void WalkObjects(walk_heap_fn /*walk*/) {}

#ifndef FRUCTUS
    [[nodiscard]] inline virtual VMSymbol* GetFieldName(
        size_t /*index*/) const {
        ErrorPrint("this object doesn't support GetFieldName\n");
        return nullptr;
    }
#endif

    /**
     * usage: new( <heap> , <additionalBytes>) VMObject( <constructor params> )
     * num_bytes parameter is set by the compiler.
     * parameter additional_bytes (a_b) is used for:
     *   - fields in VMObject, a_b must be set to
     * (numberOfFields*sizeof(VMObject*))
     *   - chars in VMString/VMSymbol, a_b must be set to (Stringlength + 1)
     *   - array size in VMArray; a_b must be set to
     * (size_of_array*sizeof(VMObject*))
     *   - fields in VMMethod, a_b must be set to (number_of_bc +
     * number_of_csts*sizeof(VMObject*))
     *
     * It answers nullptr when the heap is full and allocations may fail.
     * Being noexcept, the new-expression then skips the constructor and
     * answers nullptr too.
     */
    void* operator new(size_t numBytes, HEAP_CLS* heap,
                       size_t additionalBytes ALLOC_OUTSIDE_NURSERY_DECL)
        noexcept {
        // if outsideNursery flag is set or object is too big for nursery, we
        // allocate a mature object
        assert(IS_PADDED_SIZE(additionalBytes));

        void* result = nullptr;
#if GC_TYPE == GENERATIONAL
        if (outsideNursery) {
            result = heap->AllocateMatureObject(numBytes + additionalBytes);
        } else {
            result = heap->AllocateNurseryObject(numBytes + additionalBytes);
        }
#else
        result = heap->AllocateObject(numBytes + additionalBytes);
#endif

        assert(result != INVALID_VM_POINTER);
        return result;
    }
};
