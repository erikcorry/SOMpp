#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <type_traits>

#include "../vm/Print.h"

/**
 * A minimal growable array for trivially copyable element types, for targets
 * without std::vector. Elements are moved with memcpy/realloc.
 *
 * It is trivially destructible, so that static instances need no static
 * constructors or destructors, which not all targets run. Call Free() to
 * release the memory.
 */
template <typename T>
class SimpleVector {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    SimpleVector() = default;
    SimpleVector(const SimpleVector&) = delete;
    SimpleVector& operator=(const SimpleVector&) = delete;

    void Free() {
        free(elements);
        elements = nullptr;
        count = 0;
        capacity = 0;
    }

    void push_back(T elem) {
        if (count == capacity) {
            grow();
        }
        elements[count++] = elem;
    }

    void pop_back() { count--; }
    [[nodiscard]] T& back() { return elements[count - 1]; }
    [[nodiscard]] bool empty() const { return count == 0; }
    [[nodiscard]] size_t size() const { return count; }
    void clear() { count = 0; }

    T& operator[](size_t i) { return elements[i]; }
    const T& operator[](size_t i) const { return elements[i]; }

    T* begin() { return elements; }
    T* end() { return elements + count; }

private:
    void grow() {
        size_t const newCapacity = capacity == 0 ? 16 : capacity * 2;
        T* newElements = (T*)realloc(elements, newCapacity * sizeof(T));
        if (newElements == nullptr) {
            ErrorExit("out of memory in SimpleVector");
        }
        elements = newElements;
        capacity = newCapacity;
    }

    T* elements = nullptr;
    size_t count = 0;
    size_t capacity = 0;
};
