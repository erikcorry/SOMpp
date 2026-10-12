# Retry allocations instead of keeping 10% slack

## Background

Today an allocation never fails softly. Every heap counts what it has handed
out, and once that passes a soft limit (90% of the heap, or of the nursery)
it calls `requestGC()`. The collection happens at the next `DISPATCH_GC` in
the interpreter, which is after sends, `PUSH_BLOCK` and `PUSH_GLOBAL`. If the
heap really is full before then, the VM quits ("Failed to allocate ...").

So the last 10% of the heap has to absorb everything allocated between two
safe points. On fructus that's about 1.6K of the 16K heap: a single
`Array new: 1000` (2K) is enough to run out, and the 1.6K is never used for
anything else.

The usual fix in a VM like this: any allocation may fail, and everything that
allocates can be restarted from a safe point. The interpreter collects, then
runs the bytecode again. No GC ever happens inside C++ code, so C++ never
holds a pointer across a collection and needs no handles. That matters for
the moving GC planned in gc.md, whose "Need to fix SOM++ to do a GC on any
allocation!" this is.

This is for the host as well as fructus, to keep the `#ifdef`s down. On the
host it also makes a stress mode possible that runs a moving GC and unmaps
from-space, which would catch any C++ pointer kept across a collection.

## The protocol

1. **Allocation fails softly.** When it can't satisfy a request,
   `AllocateObject` (and the nursery and mature variants) returns `nullptr`.
   It records the size that failed and sets `gcWasRequested` plus a new
   `retryWasRequested`. It never collects itself.
2. **The placement `operator new` is `noexcept`**
   (`AbstractObject.h`), so that a new-expression checks for `nullptr` and
   skips the constructor. The `Universe::New*` helpers pass `nullptr` up.
3. **A failed operation changes nothing**, or undoes what it changed before
   returning. It can undo with the values in its C++ locals, since failing
   doesn't collect and they are all still valid. Garbage left over from an
   earlier allocation in the same operation is fine.
4. **Primitive routines return `nullptr`** for "retry". (Check that no
   primitive answers `nullptr` today. `sysGlobal_` maps a missing global to
   `nil`, for example.)
5. **The interpreter restarts the bytecode.** `DISPATCH_GC` gets the length
   of its bytecode: `DISPATCH_GC(len)`. Its slow path already runs when
   `gcWasRequested` is set. If `retryWasRequested` is set too, it first
   rewinds `pc -= len`, and then does `SAVE_PC(); startGC(); RELOAD_PC();`
   and dispatches, which runs the same bytecode again. The fast path stays
   one test, as now.
   - A send whose frame couldn't be allocated pushed no frame, so
     `RELOAD_PC()` gives the caller's next pc (`SAVE_NEXT_PC`), and
     `pc - len` is the send.
   - `INC`, `DEC`, `INC_FIELD` and `INC_FIELD_PUSH` stay `NEXT_NOGC`. They
     only allocate on `integerAdd()`'s slow path, which tests for `nullptr`
     and jumps to a restart label.
6. **Two kinds of request.** A plain `requestGC()` (`System>>fullGC`, stress
   mode) means "collect, then continue". `retryWasRequested` means "collect,
   then run the bytecode again". Otherwise `fullGC` would run itself again
   forever.
7. **Out of memory.** After the collection, the slow path checks that the
   size that failed now fits (`findFittingEntry()` on the mark-sweep heap,
   the gap on a bump or copying heap). If it doesn't, the VM quits with an
   out-of-memory error instead of looping. A heap that can grow, like the
   host's copying heap, grows instead.
8. **Drop the soft limits.** With retries, `collectionLimit` (the 90%) isn't
   needed in any heap. The collection happens when an allocation fails, which
   means fewer collections, and big objects can use the whole heap. Keep any
   growth policy that reads usage after a collection.

## The allocation sites

### Already restartable, or nearly

They change nothing before allocating, or allocate last:

| Site | Change needed |
|---|---|
| Frames: `VMMethod::Invoke`/`Invoke1` → `PushNewFrame` → `NewFrame`. Used by every send, super send, block `value` (`VMEvaluationPrimitive`) and `AbstractVMObject::Send` | `PushNewFrame` must not `SetFrame(nullptr)`, and `Invoke` must skip `CopyArgumentsFrom`. Before that, there's only `SAVE_NEXT_PC` and `SendSite::Link` in `sendMiss`, which is idempotent |
| `doPushBlock` → `NewBlock` | None |
| `INC`/`DEC`/`INC_FIELD`/`INC_FIELD_PUSH` boxing (fructus) | The `nullptr` check on the slow path, see above |
| Integer primitives that box: `+ - * / rem: % << abs`, `fromString:` via `ParseInteger`, `hashcode` of objects and strings | None: they are pure |
| `arrNew`, `arrCopy`, `clsNew`, `clsAsString`, `intAsString`, `intRange`, `strPrimSubstringFromTo`, `strCharAt` | None: one allocation, at the end |
| `strConcatenate_` | Allocate the `VMString` first and `memcpy` into it, instead of the temporary `malloc` buffer |
| Host: doubles, big integers (`InfInt` uses `malloc`, and the `VMBigInteger` is allocated last), `String>>asSymbol` (it interns after allocating), the `VMVector` primitives (the new storage is allocated before it replaces the old) | Check each one, but they look fine |

### Need changes

| Site | Problem | Plan |
|---|---|---|
| `VMSafe{Unary,Binary,Ternary}Primitive::Invoke` | They pop the arguments before calling the routine | Read the arguments with `GetStackElement`, call the routine, and pop and push only if the result isn't `nullptr` |
| `VMFrame::EmergencyFrameFrom` (`doesNotUnderstand:` and `unknownGlobal:` on the host, `escapedBlock:` on both) | It allocates in the middle of an operation, after the stack has changed | Give every frame `EXTRA_STACK_SLOTS` more slots in `NewFrame` and remove it. 2 slots are enough (see below). `RemainingStackSize()` is computed from the object size, so the existing checks never trigger |
| `triggerDoesNotUnderstand`, `SendUnknownGlobal`, host `doSuperSend` DNU | The argument array is allocated first, which is fine. But then the arguments are popped into it and `Send` pushes before `Invoke` allocates the frame | If the frame allocation fails, undo the pushes and push the receiver and arguments back from the array |
| `doReturnNonLocal`, `escapedBlock:` path | `popFrame()` and the argument pops come before the `Send` frame | Fructus: make it a fatal error in C++, like DNU and unknown global already are (Smalltalk-fructus `escapedBlock:` only calls `error:`). Host: undo, or check the frame fits before `popFrame()` |
| Host `perform:`, `perform:inSuperclass:`, `perform:withArguments:`, `perform:withArguments:inSuperclass:`, `Method>>invokeOn:with:`, `Primitive>>invokeOn:with:` | They rewrite the stack (pop the selector or array, push the arguments), then `Invoke`. The invokable may be an allocating primitive, not just a method | Undo when `Invoke` fails: pop what was pushed and push back what was popped, from the C++ locals |
| `VMVector` error paths (`Send(error:)`, `Send(println)`) | They send from C++ in the middle of a primitive | Fatal errors, or undo like the DNU path |

How many extra slots: `doesNotUnderstand:arguments:` needs 3 (the receiver,
the selector and the argument array), and the failed send's receiver and
arguments free at least 1 (a unary send), so 2. `unknownGlobal:` and
`escapedBlock:` need 2 each with at least 1 free, so 1. On fructus none of
these sends exist, so `EXTRA_STACK_SLOTS` can be 0 there.

### Outside the interpreter

The parser and compiler (class loading at startup, `System>>load:`, the
shell, the fructus image writer on the host) allocate with many C++ locals
and can't be restarted. Startup on fructus runs with an empty heap.

## Decisions

- **Allocation in the compiler on the host.** On its first try,
  `System>>load:` compiles like any other primitive, and any allocation may
  fail. If one does, the half-built class is dropped as garbage, and the
  primitive asks for a retry. The interpreter collects and runs `load:`
  again. This second try compiles in a "must succeed" scope, where an
  allocation that fails is a fatal out-of-memory error. The primitive has to
  know which try it's on: a flag set on the failure and cleared by the retry
  is enough. Startup, the shell and the image writer have no bytecode to
  restart, so they always run in that scope.
  - The compiler is abandoned with a C++ exception, `CompilationAbandoned`,
    instead of checking for nullptr all through it. Fructus has no compiler
    and no exceptions, so all of this is in `src/compiler/CompileAttempt.*`,
    which fructus doesn't build. The allocation helpers that fructus shares
    (`Universe::New*`, `NewSymbol`, the primitive and trivial method
    factories) call `AbandonIfCompiling()` when an allocation
    answers nullptr. It throws only during a first try, and is a no-op on
    fructus. The throw can't come from the allocation itself: `operator new`
    is `noexcept`.
  - `CompileAttempt::Run()` wraps `System>>load:` and the lazy loading of
    the block classes in `NewBlock`, which gets the class before allocating
    the block. Nothing becomes reachable before the compilation is done:
    `LoadClass` sets the global only at the end, and a superclass loaded on
    the way is complete.
  - In that scope, an allocation that doesn't fit in the nursery goes to
    old space, so that a small nursery doesn't make compilation fail. For
    `GenerationalHeap` that is `AllocateMatureObject()`, which `malloc`s,
    so old space effectively grows: this is partly the "let the compiler
    expand the heap" option.
  - An object allocated old this way must be marked old
    (`MASK_OBJECT_IS_OLD`, as `NewArray` does for big arrays). Its
    constructor clears the GC field, so the next minor collection marks it,
    before it walks the roots: it marks all of old space. The stores
    that initialize it may store nursery pointers that the write barrier
    doesn't see. Constructors skip the barrier on purpose, because a new
    object is normally young.
  - So instead of adding barriers to the initializing stores, a diversion
    declares the remembered set bankrupt: it sets a flag on the heap, and
    the next minor collection scans all of old space for pointers into the
    nursery. For `GenerationalHeap` that means walking `allocatedObjects`
    instead of only `oldObjsWithRefToYoungObjs`, then clearing the flag.
    The common case keeps its cheap initialization, and only the rare
    diversion pays for the full scan.
  - The flag stays set from the first diversion until that minor
    collection, however many diversions come in between. A major
    collection doesn't need it: `GenerationalCollector::Collect()` always
    runs a minor collection first, which consumes the flag and leaves the
    nursery empty, so no pointers into the nursery are left for the major
    one.
  - The scan must only visit the objects that were old when it started. A
    minor collection promotes survivors with `AllocateMatureObject()`,
    which appends to `allocatedObjects`, so iterating the vector directly
    would be invalidated. Iterate by index up to the size it had at the
    start: the promoted objects are walked by the copying itself.
  - The scan visits dead objects too, so every object in the heap must be
    walkable from its constructor on: every reference `WalkObjects` visits
    holds a tagged integer or a pointer to an object that still exists, and
    references that may be nullptr are skipped by `WalkObjects` itself. This
    relies on constructors, not on the allocators, which don't zero memory.
    The few references a constructor leaves (frame arguments, method
    bytecodes, the copied fields of an expanded array) are filled in with no
    allocation in between, so neither a collection nor an abandon point sees
    them unset. A compilation also stores its objects into older ones (the
    symbol table, a superclass it loads, the globals) only once they are
    complete. `IsValidObject.cpp` used to make deliberately invalid objects
    in the heap to read their vtables; it now makes them on the stack, so the
    copying only asserts that a reference isn't nullptr.
  - The heaps without a separate old space (mark-sweep, copying) have
    nowhere to divert to, so failing there is fatal. The copying heap only
    grows at a collection.
- **`escapedBlock:` is a fatal C++ error on fructus**, like DNU and unknown
  global. `EXTRA_STACK_SLOTS` is 0 on fructus and 2 on the host.
- **Stress mode.** The "fail once" mode is added next to today's
  `-gc-stress`, which stays as it is. This might not be upstreamed, and
  someone may depend on the old mode. Today's mode becomes a plain
  `requestGC()` after every allocation, "collect, then continue", which still
  works under the new protocol.
- **Unmapping from-space** is a follow-up, not part of this work.

## Stress mode

`-gc-stress-retry` (debug builds) sits next to `-gc-stress`, and makes
every allocation fail once. Failing only the allocation right after a
successful one would livelock a bytecode that allocates twice: the retry's
second allocation would fail again, forever. Instead the heap keeps one flag,
"the next allocation fails":

- An allocation that may fail, with the flag set, clears it and fails.
- A collection for a retry leaves the flag clear, so the retried bytecode
  succeeds however many times it allocates.
- An allocation that succeeds while the flag is clear asks for a plain
  collection, which the next `DISPATCH_GC` runs. A plain collection sets the
  flag again.

So each allocating bytecode costs two collections, a failure and a retry.
TestHarness on the generational heap fails 22,045 allocations and collects
44,092 times. In the "must succeed" scope it can't fail allocations, so it is
off there; a compilation fails on its first try and succeeds on the retry.
Until step 7 lets allocations fail everywhere, the mode alone makes
`Interpreter::Start()` run where they may.

Follow-up: combined with a copying heap that unmaps (`mprotect`) from-space
after each collection, instead of poisoning the objects as `DebugCopyingHeap`
does, any C++ pointer kept across a collection would fault right where it's
used.

## Order of work

All of this is done on the host first, and tested with a moving GC
(`CopyingHeap`, `DebugCopyingHeap`, `GenerationalHeap`). The fructus port
comes once that works. Fructus is non-moving today, but a moving GC is the
next thing planned for it, so nothing here may rely on objects staying where
they are: a C++ pointer is never kept across a collection, even on fructus.

1. `noexcept` `operator new`, `nullptr` from the heaps, and the
   `retryWasRequested` flag. Keep the soft limit for now.
2. `VMSafe*Primitive::Invoke`, `PushNewFrame`/`Invoke`, and
   `DISPATCH_GC(len)` with the rewind.
3. The INC family's slow path, `strConcatenate_`, and `escapedBlock:` as a
   fatal error on fructus.
4. `EXTRA_STACK_SLOTS`, removing `EmergencyFrameFrom`, and the undo paths
   for DNU, unknown global, perform and invokeOn on the host.
5. The "must succeed" scope, with the diversion to old space, and the
   single retry in `System>>load:`.
6. The "fail once" stress mode, run over the TestHarness with every host
   GC.
7. Remove the soft limits, and add the out-of-memory check after the
   collection.

## How step 7 came out

- `Heap::FullGC()`, after a collection for a retry, asks the heap's
  `MakeRoomFor(size)` whether the failed size fits now, and quits with an
  out-of-memory error if it doesn't. The copying heaps double until it fits.
  The heaps that `malloc` objects (debug mark-sweep, paged) never fail. The
  generational heap's nursery is empty after a collection, and an object
  bigger than the nursery goes to old space straight away, even where
  allocations may fail.
- The 90% soft limit is gone from the copying heaps, mark-sweep and the
  nursery. The debug mark-sweep and paged heaps keep theirs, since it is
  their growth policy (twice the live size). Fructus keeps the mark-sweep
  soft limit until it runs the retries, since all its allocations must
  succeed.
- The "may fail" scope is around the calls of `Interpreter::Start()`, not
  in it: in it, its destructor made the interpreter loop about 10% slower
  on List.
- A compilation on a copying heap may fail any number of times: there is
  no other space to put what doesn't fit, but the heap grows at a
  collection. The first failure gets a plain collection, since it usually
  only means the semispace was nearly full, and each later one doubles the
  heap (`COMPILATION_RETRIES_GROW_HEAP`, `GrowAtNextCollection()`). The
  other heaps still retry once, where allocations must succeed.
- `Heap::SafePoint()`, which the interpreter calls between bytecodes, sees
  whether a retry made progress: after a collection for a retry it asks to
  be called again, which only notes that a bytecode completed. If another
  retry comes first, the retry failed too: a copying heap doubles in that
  collection, and allocations must succeed until a bytecode completes. A
  full fixed-size heap then quits with out-of-memory, and the generational
  heap puts the object in old space. A failed try of a compilation counts
  as progress, since compilations have their own policy. Tested by making
  the stress mode fail every retry once more: TestHarness passes on the
  generational and mark-sweep heaps.
