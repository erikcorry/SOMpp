#pragma once

/*
 * Compiling a class while the interpreter runs (System>>load:, a block
 * class) may fail once for lack of memory. The first try compiles with
 * allocations that may fail. When one does, the compilation is abandoned by
 * throwing, the half-built class is garbage, and the caller answers nullptr
 * so that the interpreter collects and runs the bytecode again. The retry
 * compiles where allocations must succeed.
 *
 * Fructus has no compiler and no exceptions, so this is host only. Code that
 * fructus shares calls AbandonCompilationIfTrying() where an allocation
 * answered nullptr, and that is a no-op there.
 */

#ifdef FRUCTUS
inline void AbandonCompilationIfTrying() {}
#endif

/** Answers `allocated`, after abandoning a compilation if it is nullptr. */
template <class T>
T* AbandonCompilationIfNull(T* allocated);

#ifndef FRUCTUS

  #include <assert.h>

  #include "../misc/defs.h"
  #include "../vmobjects/AbstractObject.h"

class VMClass;

/** Throws, while a compilation may fail, to abandon it. */
void AbandonCompilationIfTrying();

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
    friend void AbandonCompilationIfTrying();

    static bool trying;
    // The last try failed, so the next one is the retry.
    static bool failedOnce;
};

template <class Compile>
VMClass* CompileAttempt::Run(Compile compile) {
    if (trying) {
        // a nested compilation is abandoned with the outer one
        return compile();
    }
    if (failedOnce) {
        failedOnce = false;
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
        // AbandonCompilationIfTrying()
        assert(result == nullptr);
        failedOnce = true;
        return nullptr;
    }
    return result;
}

#endif

template <class T>
T* AbandonCompilationIfNull(T* allocated) {
    if (allocated == nullptr) {
        AbandonCompilationIfTrying();
    }
    return allocated;
}
