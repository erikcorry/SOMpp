# Keep the program counter in a register

## Today

`Interpreter::bytecodeIndexGlobal` is the interpreter's program counter: the
index of the next bytecode in `currentBytecodes`, the bytecodes of the running
method. It is a cached copy of the running frame's `VMFrame::bytecodeIndex`,
and while a frame runs, the cached copy is the up-to-date one.

- Every handler starts with `PROLOGUE(n)`, which advances the index by the
  length of the bytecode before doing the work. Handlers find their operands
  behind it, e.g. `doPushLocal(bytecodeIndexGlobal - 3)`. Jumps read their
  offset at `bytecodeIndexGlobal + 1` and then add it, or skip with `+= 3`.
- Dispatch is `goto *loopTargets[currentBytecodes[bytecodeIndexGlobal]]`.
- `SetFrame()` is the one place that switches frames: it writes the index
  back to the old frame and loads `frame`, `method`, `bytecodeIndexGlobal`,
  and `currentBytecodes` from the new one. A send reaches it through
  `PushNewFrame()`, a return through `popFrame()`.
- `VMMethod::Invoke`/`Invoke1` also write the index back to the caller's
  frame before pushing the new one, which is redundant with `SetFrame()`.
- `startGC()` writes the index back before collecting, then reloads `method`
  and `currentBytecodes`, which a moving GC may have moved.
- `doReturnNonLocal()` writes it back before replacing the frame with an
  emergency frame, and `ResetBytecodeIndex()` sets it to 0.

## The problem

The index lives in memory, in a static, so every bytecode loads it,
adds the length, and stores it back, and dispatch then reads
`currentBytecodes` from memory as well. GCC can keep the index in a register
only across short stretches, because `Invoke`, `SetFrame()`, the GC, and the
out-of-line `do*` helpers all read the field.

This costs code in every handler.

## The idea

Make the program counter a local in `Interpreter::Start()`, so the register
allocator can keep it in a register:

- Preferably a pointer into the bytecodes, `uint8_t* pc`, instead of an index.
  Dispatch becomes `goto *loopTargets[*pc]`, so `currentBytecodes` isn't
  needed while running, and operands are `pc[-2]`, `pc[1]`, etc.
- Write it back to the frame, as an index (`pc - bytecodes`), only where
  something can observe it:
  - before a send or a primitive call, i.e. before `Invoke`/`Invoke1`, which
    may push a new frame, look at the stack, or trigger `doesNotUnderstand:`;
  - before a GC, which may move the method, and so the bytecodes;
  - before anything that can print a stack trace or replace the frame
    (`EmergencyFrameFrom`, non-local returns, `escapedBlock:`).
- Reload it after anything that can switch frames or move objects: after a
  send returns control to the dispatch loop, after a return (`popFrame()`),
  and after a GC.
- Pass operands to the `do*` helpers as arguments instead of having them read
  the index, and make the helpers that switch frames tell `Start()` to reload.

A pointer must be recomputed after a GC, because the bytecodes live inside the
method object. With the mark-sweep GC used on fructus nothing moves, but the
generational and copying GCs do move methods, so the reload after a GC is
needed in general.

## Where to be careful

- The send and return paths currently get the program counter through
  `SetFrame()`. They need an explicit "save before, reload after" protocol,
  and `SetFrame()` should stop saving and restoring the index itself, or the
  two will disagree.
- Primitives that call back into the interpreter (`AbstractVMObject::Send`,
  used for `doesNotUnderstand:`, `unknownGlobal:`, `escapedBlock:`, and by
  `VMVector`) must see an up-to-date saved index.
- `VMFrame::PrintStack()` (debug only) prints the frame's saved index, which
  is out of date for the running frame today, too.

## Expected effect

Smaller handlers and faster dispatch: one register increment instead of a
load, add, and store per bytecode, and one memory access less per dispatch.
Measure on x64 with `build-release/SOM++.map`;
