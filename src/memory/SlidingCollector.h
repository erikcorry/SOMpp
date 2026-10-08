#pragma once

#include "GarbageCollector.h"

class SlidingHeap;

/**
 * Marks the live objects, then slides them up, to the top of the heap in a
 * major collection, and to the old objects in a minor one, see SlidingHeap.h
 * and ideas/gc.md.
 */
class SlidingCollector : public GarbageCollector<SlidingHeap> {
public:
    explicit SlidingCollector(SlidingHeap* h) : GarbageCollector(h) {};

    void Collect() override;

private:
    void markLiveObjects(bool major);
    void rescanGrayPagelets();
    void countLiveGranules();
    void slide();
#if DEBUG
    void verifyRememberedSet();
    void verify();
#endif
};
