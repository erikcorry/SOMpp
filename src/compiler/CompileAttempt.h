#pragma once

/*
 * Compiling a class while the interpreter runs (System>>load:, a block
 * class) may fail for lack of memory. A try compiles with allocations that
 * may fail. When one does, the compilation is abandoned by throwing, the
 * half-built class is garbage, and the caller answers nullptr so that the
 * interpreter collects and runs the bytecode again.
 *
 * Most heaps compile the retry where allocations must succeed: the
 * generational heap puts what doesn't fit in the nursery in old space. A
 * copying heap has nowhere else to put it, but it can grow at a collection.
 * So there every try may fail. The first failure usually means only that the
 * semispace was nearly full, so a plain collection follows it. Each later one
 * doubles the heap.
 *
 * Fructus has no compiler and no exceptions, so this is host only. Code that
 * fructus shares calls AbandonIfCompiling() where an allocation answered
 * nullptr, and that is a no-op there.
 */

#ifdef FRUCTUS
inline void AbandonIfCompiling() {}
#endif

/** Answers `allocated`, after abandoning a compilation if it is nullptr. */
template <class T>
T* AbandonIfCompilingAndNull(T* allocated);

#ifndef FRUCTUS

  #include <assert.h>

  #include "../misc/defs.h"
  #include "../vmobjects/AbstractObject.h"

class VMClass;

/**
 * Throws to abandon the compilation that is in its first try, if there is
 * one. No other compilation gets here, since its allocations don't fail.
 */
void AbandonIfCompiling();

struct CompilationAbandoned {};

class CompileAttempt {
public:
    /**
     * Runs `compile`, which answers a class. Answers nullptr, with a retry
     * requested, if an allocation failed.
     */
    template <class Compile>
    static VMClass* Run(Compile compile);

private:
    friend void AbandonIfCompiling();

    static bool trying;
    // How many tries of the compilation failed. The next one is a retry if
    // it isn't 0.
    static size_t failures;
};

template <class Compile>
VMClass* CompileAttempt::Run(Compile compile) {
    if (trying) {
        // a nested compilation is abandoned with the outer one
        return compile();
    }
    if (failures != 0 && !HEAP_CLS::COMPILATION_RETRIES_GROW_HEAP) {
        failures = 0;
        HEAP_CLS::MustSucceedScope const mustSucceed;
        return compile();
    }

    HEAP_CLS::MustSucceedScope const mayFail(false);
    trying = true;
    VMClass* result = nullptr;
    try {
        result = compile();
    } catch (CompilationAbandoned const&) {
        result = nullptr;
    }
    trying = false;

    if (GetHeap<HEAP_CLS>()->isRetryRequested()) {
        // without a throw, an allocation site is missing its call to
        // AbandonIfCompiling()
        assert(result == nullptr);
        if (HEAP_CLS::COMPILATION_RETRIES_GROW_HEAP && failures != 0) {
            GetHeap<HEAP_CLS>()->GrowAtNextCollection();
        }
        GetHeap<HEAP_CLS>()->NoteProgress();
        failures++;
        return nullptr;
    }
    failures = 0;
    return result;
}

#endif

template <class T>
T* AbandonIfCompilingAndNull(T* allocated) {
    if (allocated == nullptr) {
        AbandonIfCompiling();
    }
    return allocated;
}
