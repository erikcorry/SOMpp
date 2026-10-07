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

#include <stdlib.h>

#include "../misc/defs.h"
#include "../vmobjects/ObjectFormats.h"
#include "GarbageCollector.h"

/*
 * macro for padding - only word-aligned memory must be allocated
 */
#define PADDED_SIZE(N) \
    (((((size_t)(N)) + (sizeof(void*) - 1)) & ~(sizeof(void*) - 1)))

#define IS_PADDED_SIZE(N) ((N) == PADDED_SIZE((N)))

using namespace std;

/*
 * If true, request a GC after every single allocation.
 */
#if DEBUG
extern bool gcStressMode;
#else
  #define gcStressMode false
#endif

/*
 * If true, fail every allocation that may fail once, to run every bytecode
 * and compilation again after a collection. An allocation that follows a
 * retry succeeds, and asks for a plain collection, after which allocations
 * fail again. That way a bytecode that allocates more than once completes.
 */
#if DEBUG
extern bool gcStressRetryMode;
#else
  #define gcStressRetryMode false
#endif

template <class HEAP_T>
class Heap {
    friend class GarbageCollector<HEAP_T>;
    friend HEAP_T;

private:
    explicit Heap(GarbageCollector<HEAP_T>* const gc) : gc(gc) {}

public:
    static void InitializeHeap(size_t objectSpaceSize);
    static void DestroyHeap();

    ~Heap();
    /**
     * Asks for a collection at the next safe point, after which the
     * interpreter continues.
     */
    inline void requestGC() { gcWasRequested = true; }
    inline void resetGCTrigger() {
        gcWasRequested = false;
        retryWasRequested = false;
    }
    bool isCollectionTriggered() { return gcWasRequested; }
    /**
     * An allocation failed: the interpreter must collect, then run the
     * bytecode that failed again.
     */
    bool isRetryRequested() { return retryWasRequested; }
    /** The size of the last allocation that failed. */
    [[nodiscard]] size_t GetFailedAllocationSize() const {
        return failedAllocationSize;
    }
    void FullGC();

    /**
     * In gc-stress-retry mode, answers whether to fail an allocation of
     * `size` bytes that would succeed, and asks for the retry if it does.
     */
    bool StressFailsAllocation(size_t size) {
        if (allocationMustSucceed) {
            return false;
        }
        if (stressFailsNext) {
            stressFailsNext = false;
            allocationFailed(size);
            return true;
        }
        requestGC();
        return false;
    }

    inline void FreeObject(AbstractVMObject* o) { free(o); }

    /**
     * Whether an allocation that doesn't fit is a fatal out-of-memory error.
     * If it isn't, the allocation answers nullptr and asks for a retry
     * instead, and the caller must give up and change nothing.
     */
    [[nodiscard]] bool AllocationMustSucceed() const {
        return allocationMustSucceed;
    }

    /**
     * Sets whether allocations must succeed while it's in scope. Collections
     * always run in a scope where they must, and so does everything outside
     * the interpreter.
     */
    class MustSucceedScope {
    public:
        explicit MustSucceedScope(bool mustSucceed = true)
            : heap(Heap<HEAP_T>::theHeap),
              outerValue(heap->allocationMustSucceed) {
            heap->allocationMustSucceed = mustSucceed;
        }
        ~MustSucceedScope() { heap->allocationMustSucceed = outerValue; }
        MustSucceedScope(const MustSucceedScope&) = delete;
        MustSucceedScope& operator=(const MustSucceedScope&) = delete;

    private:
        Heap<HEAP_T>* const heap;
        bool const outerValue;
    };

protected:
    GarbageCollector<HEAP_T>* const gc;

    /**
     * Called by a heap when an allocation of `size` bytes doesn't fit. Quits
     * if allocations must succeed, and otherwise asks for a retry and answers
     * nullptr for the allocation to return.
     */
    void* allocationFailed(size_t size);

private:
    template <class HEAP_U>
    friend HEAP_U* GetHeap();
    static HEAP_T* theHeap;

    // flag that shows if a Collection is triggered
    bool gcWasRequested{false};
    bool retryWasRequested{false};
    size_t failedAllocationSize{0};

    // In gc-stress-retry mode, whether the next allocation that may fail
    // fails. A collection for a retry leaves it false, so the retry succeeds.
    bool stressFailsNext{true};

    // Until the interpreter can restart bytecodes, every allocation must
    // succeed.
    bool allocationMustSucceed{true};
};

template <class HEAP_T>
HEAP_T* Heap<HEAP_T>::theHeap;

template <class HEAP_T>
inline HEAP_T* GetHeap() __attribute__((always_inline));
template <class HEAP_T>
HEAP_T* GetHeap() {
    return Heap<HEAP_T>::theHeap;
}
