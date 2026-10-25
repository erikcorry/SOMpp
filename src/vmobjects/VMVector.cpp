#include "../vmobjects/VMVector.h"

#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <string.h>

#include "../interpreter/Interpreter.h"
#include "../misc/defs.h"
#include "../vm/Globals.h"
#ifndef FRUCTUS
  #include "../vm/Symbols.h"
#endif
#include "../vm/Universe.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMSafePrimitive.h"

const size_t VMVector::VMVectorNumberOfFields = 3;

VMVector::VMVector(vm_oop_t first, vm_oop_t last, VMArray* storage)
    : VMObject(VMVectorNumberOfFields, sizeof(VMVector)),
      first(store_with_separate_barrier(first)),
      last(store_with_separate_barrier(last)),
      storage(store_with_separate_barrier(storage)) {
    static_assert(VMVectorNumberOfFields == 3);
}

#ifndef FRUCTUS
// for the Vector primitives, which fructus doesn't have
vm_oop_t VMVector::AWFYGetStorage(som_int_t index) {
    // This is a method that is used by the AWFY tests, it does not handle
    // 1-0 indexing conversion
    som_int_t const first = SMALL_INT_VAL(load_ptr(this->first));
    VMArray const* const storage = load_ptr(this->storage);

    if (index > storage->GetNumberOfIndexableFields()) {
        return load_ptr(nilObject);  // AWFY does not handle an IndexOutOfBounds
                                     // in this case
    }
    vm_oop_t returned = storage->GetIndexableField(
        (first - 1) + (index - 1));  // Convert to 0-indexing
    return returned;
}

vm_oop_t VMVector::GetStorage(som_int_t index) {
    som_int_t const first = SMALL_INT_VAL(load_ptr(this->first));
    som_int_t const last = SMALL_INT_VAL(load_ptr(this->last));
    VMArray const* const storage = load_ptr(this->storage);

    if (index < 1 || index > last - first) {
        return IndexOutOfBounds(first + last - 1, index);
    }
    vm_oop_t returned = storage->GetIndexableField(
        (first - 1) + (index - 1));  // Convert to 0-indexing
    return returned;
}

/* Returns the value currently held at that location */
vm_oop_t VMVector::SetStorage(som_int_t index, vm_oop_t value) {
    som_int_t const first = SMALL_INT_VAL(load_ptr(this->first));
    som_int_t const last = SMALL_INT_VAL(load_ptr(this->last));
    VMArray* const storage = load_ptr(this->storage);
    if (index < 1 || index > first + last) {
        return IndexOutOfBounds(first + last - 1, index);
    }
    vm_oop_t curVal = storage->GetIndexableField(first + index - 2);
    storage->SetIndexableField(first + index - 2, value);
    return curVal;
}

/* AWFY Vector can expand on at:put: core-lib vector cannot*/
bool VMVector::SetStorageAWFY(som_int_t index, vm_oop_t value) {
    som_int_t const first = SMALL_INT_VAL(load_ptr(this->first));
    som_int_t const last = SMALL_INT_VAL(load_ptr(this->last));
    VMArray* storage = load_ptr(this->storage);

    if (index > storage->GetNumberOfIndexableFields()) {
        // Expand the array
        VMArray* newStorage = Universe::NewExpandedArrayFromArray(
            storage->GetNumberOfIndexableFields() * 2, storage);
        if (newStorage == nullptr) {
            return false;
        }

        newStorage->SetIndexableField(index - 1, value);

        this->storage = store_ptr(this->storage, newStorage);

    } else {
        vm_oop_t newLast = NEW_INT(last + 1);
        if (newLast == nullptr) {
            return false;
        }
        // Just set the new value
        storage->SetIndexableField(first + index - 2, value);
        this->last = store_ptr(this->last, newLast);
    }
    return true;
}

bool VMVector::Append(vm_oop_t value) {
    som_int_t const last = SMALL_INT_VAL(load_ptr(this->last));
    VMArray* storage = load_ptr(this->storage);

    vm_oop_t newLast = NEW_INT(last + 1);
    if (newLast == nullptr) {
        return false;
    }

    if (last >= storage->GetNumberOfIndexableFields()) {  // Expand the array
        // Expand by the correct amount *2 by default in the native som
        // implementation
        VMArray* newStorage = Universe::NewExpandedArrayFromArray(
            storage->GetNumberOfIndexableFields() * 2, storage);
        if (newStorage == nullptr) {
            return false;
        }

        storage->CopyIndexableFieldsTo(newStorage);
        newStorage->SetIndexableField(last - 1, value);

        SetField(2 /* storage */, newStorage);

    } else {  // Just set the new value
        storage->SetIndexableField(last - 1, value);
    }

    this->last = store_ptr(this->last, newLast);
    return true;
}

vm_oop_t VMVector::RemoveLast() {
    const som_int_t last = SMALL_INT_VAL(load_ptr(this->last));
    const som_int_t first = SMALL_INT_VAL(load_ptr(this->first));

    // Throw an error correctly (error: method in som)
    if (last == first) {
        return sendError("Vector: error when removing Last item.");
    }

    return remove(last - first);  // last element, 1-based indexing
}

vm_oop_t VMVector::RemoveFirst() {
    // This method will just increment the first index
    som_int_t first = SMALL_INT_VAL(load_ptr(this->first));

    // Throw an error correctly (error: method in som)
    if (first >= SMALL_INT_VAL(load_ptr(this->last))) {
        return sendError("Vector: error when removing First item.");
    }

    vm_oop_t newFirst = NEW_INT(first + 1);  // Increment the first index
    if (newFirst == nullptr) {
        return nullptr;
    }

    // This is 1 because GetIndexableField handles 1 to 0 indexing
    vm_oop_t itemToRemove = GetStorage(1);
    this->first = store_ptr(this->first, newFirst);
    return itemToRemove;
}

vm_oop_t VMVector::RemoveObj(vm_oop_t other) {
    const som_int_t first = SMALL_INT_VAL(load_ptr(this->first));
    const som_int_t last = SMALL_INT_VAL(load_ptr(this->last));
    VMArray const* const storage = load_ptr(this->storage);

    for (som_int_t i = first - 1; i < last - 1; ++i) {
        vm_oop_t current = storage->GetIndexableField(i);

        // Check where integers are tagged or references can be checked
        if (current == other) {
            if (remove(i - first + 2) == nullptr) {  // uses 1-based indexing
                return nullptr;
            }
            return load_ptr(trueObject);
        }
    }
    return load_ptr(falseObject);
}

vm_oop_t VMVector::remove(som_int_t index) {
    const som_int_t first = SMALL_INT_VAL(load_ptr(this->first));
    const som_int_t last = SMALL_INT_VAL(load_ptr(this->last));
    VMArray* storage = load_ptr(this->storage);

    vm_oop_t newLast = NEW_INT(last - 1);
    if (newLast == nullptr) {
        return nullptr;
    }

    vm_oop_t itemToRemove = GetStorage(index);

    // Shift all elements after the index to the left
    for (som_int_t i = index; i < last - first; ++i) {
        storage->SetIndexableField(first + i - 1,
                                   storage->GetIndexableField(first + i));
    }

    this->last = store_ptr(this->last, newLast);

    return itemToRemove;
}

bool VMVector::RemoveAll() {
    vm_oop_t one = NEW_INT(1);
    VMArray const* const storage = load_ptr(this->storage);
    VMArray* newArray =
        Universe::NewArray(storage->GetNumberOfIndexableFields());
    if (one == nullptr || newArray == nullptr) {
        return false;
    }
    this->first = store_ptr(this->first, one);
    this->last = store_ptr(this->last, one);
    this->storage = store_ptr(this->storage, newArray);
    return true;
}

vm_oop_t VMVector::copyStorageArray() {
    const som_int_t first = SMALL_INT_VAL(load_ptr(this->first));
    const som_int_t last = SMALL_INT_VAL(load_ptr(this->last));
    VMArray const* const storage = load_ptr(this->storage);

    VMArray* result = Universe::NewArray(last - first);
    if (result == nullptr) {
        return nullptr;
    }
    for (som_int_t i = first - 1; i < last - 1; ++i) {
        result->SetIndexableField(i - first + 1, storage->GetIndexableField(i));
    }

    return result;
}

/* Rename as a more specific error function */
vm_oop_t VMVector::IndexOutOfBounds(size_t maxSize, size_t indexAccessed) {
    char message[64];
    snprintf(message, sizeof(message),
             "Vector[1..%zu]: Index %zu out of bounds", maxSize,
             indexAccessed);
    return sendError(message);
}

// Sends #error: to the vector, in place of the primitive's send, see
// VMSafePrimitive::SendError().
vm_oop_t VMVector::sendError(const char* message) {
    return VMSafePrimitive::SendError(message);
}
#endif
