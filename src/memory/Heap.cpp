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

#include "Heap.h"

#include <string.h>

#include "../misc/defs.h"
#include "../vm/Print.h"
#ifndef FRUCTUS
#include "CopyingHeap.h"         // NOLINT(misc-include-cleaner)
#include "DebugCopyingHeap.h"    // NOLINT(misc-include-cleaner)
#include "DebugMarkSweepHeap.h"  // NOLINT(misc-include-cleaner)
#include "GenerationalHeap.h"    // NOLINT(misc-include-cleaner)
#endif
#include "MarkSweepHeap.h"       // NOLINT(misc-include-cleaner)
#ifndef FRUCTUS
#include "PagedMarkSweepHeap.h"  // NOLINT(misc-include-cleaner)
#endif
#include "SlidingHeap.h"         // NOLINT(misc-include-cleaner)

#if DEBUG
bool gcStressMode = false;
bool gcStressRetryMode = false;
#else
// it's defined as false
#endif

template <class HEAP_T>
void Heap<HEAP_T>::InitializeHeap(size_t objectSpaceSize) {
    if (theHeap) {
        ErrorPrint(
            "Warning, reinitializing already initialized Heap, "
            "all data will be lost!\n");
        delete theHeap;
    }
    theHeap = new HEAP_CLS(objectSpaceSize);
}

template <class HEAP_T>
void Heap<HEAP_T>::DestroyHeap() {
    delete theHeap;
}

template <class HEAP_T>
Heap<HEAP_T>::~Heap() {
    delete gc;
}

template <class HEAP_T>
void Heap<HEAP_T>::FullGC() {
    // the collector itself allocates, to copy objects
    MustSucceedScope const mustSucceed;
    bool const retry = retryWasRequested;
    size_t const size = failedAllocationSize;
    stressFailsNext = !retry;
    gc->Collect();

    // The bytecode whose allocation failed runs again, so the allocation
    // must fit now, or it would fail forever.
    if (retry && !static_cast<HEAP_T*>(this)->MakeRoomFor(size)) {
        ErrorPrintf("\nFailed to allocate %zu bytes.\n", size);
        Quit(-1);
    }
}

template <class HEAP_T>
void Heap<HEAP_T>::SafePoint() {
    if (!retryWasRequested) {
        // a bytecode completed since the last retry
        retryAwaitingProgress = false;
        mustSucceedUntilProgress = false;
        if (collectionWasRequested) {
            FullGC();
        } else {
            gcWasRequested = false;
        }
        return;
    }

    if (retryAwaitingProgress) {
        // The retry failed too. A copying heap grows in this collection. A
        // full fixed-size heap quits where the retry fails again, and the
        // generational heap puts the object in old space.
        static_cast<HEAP_T*>(this)->GrowAtNextCollection();
        mustSucceedUntilProgress = true;
    }
    FullGC();
    retryAwaitingProgress = true;
    gcWasRequested = true;
}

template <class HEAP_T>
void* Heap<HEAP_T>::allocationFailed(size_t size) {
    if (AllocationMustSucceed()) {
        ErrorPrintf("\nFailed to allocate %zu bytes.\n", size);
        Quit(-1);
    }
    failedAllocationSize = size;
    gcWasRequested = true;
    retryWasRequested = true;
    return nullptr;
}

// Instantitate Template for the heap classes
template void Heap<HEAP_CLS>::InitializeHeap(size_t);
template void Heap<HEAP_CLS>::DestroyHeap();
template void Heap<HEAP_CLS>::FullGC();
template void Heap<HEAP_CLS>::SafePoint();

class GenerationalHeap;
template GenerationalHeap* Heap<GenerationalHeap>::theHeap;
template Heap<GenerationalHeap>::~Heap();
template void* Heap<GenerationalHeap>::allocationFailed(size_t);

class CopyingHeap;
template CopyingHeap* Heap<CopyingHeap>::theHeap;
template Heap<CopyingHeap>::~Heap();
template void* Heap<CopyingHeap>::allocationFailed(size_t);

class MarkSweepHeap;
template MarkSweepHeap* Heap<MarkSweepHeap>::theHeap;
template Heap<MarkSweepHeap>::~Heap();
template void* Heap<MarkSweepHeap>::allocationFailed(size_t);

class DebugMarkSweepHeap;
template DebugMarkSweepHeap* Heap<DebugMarkSweepHeap>::theHeap;
template Heap<DebugMarkSweepHeap>::~Heap();
template void* Heap<DebugMarkSweepHeap>::allocationFailed(size_t);

class PagedMarkSweepHeap;
template PagedMarkSweepHeap* Heap<PagedMarkSweepHeap>::theHeap;
template Heap<PagedMarkSweepHeap>::~Heap();
template void* Heap<PagedMarkSweepHeap>::allocationFailed(size_t);

class DebugCopyingHeap;
template void* Heap<DebugCopyingHeap>::allocationFailed(size_t);

class SlidingHeap;
template SlidingHeap* Heap<SlidingHeap>::theHeap;
template Heap<SlidingHeap>::~Heap();
template void* Heap<SlidingHeap>::allocationFailed(size_t);
