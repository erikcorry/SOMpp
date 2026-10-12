# Order object fields for fructus' short offsets

## Background

fructus has short encodings for loads and stores at the offsets 0, 2, 4, 6,
and 8 from a register (and 1 and 3 for byte loads and stores). A field further
into an object costs a longer instruction every time it is used. Making the
interpreter an object with its hottest fields first saved 406 bytes of the
interpreter's code; the same applies to the VM objects the interpreter
touches for almost every bytecode.

Every VM object starts with two words that can't move:

| offset | field | used by |
|---|---|---|
| 0 | vtable pointer | virtual calls, e.g. `GetClass()`, `WalkObjects()` |
| 2 | `gcField` (`VMObjectBase`) | the GC only |

So only **4, 6, and 8** are left for each class's own fields.

Offsets on fructus, as the compiler lays them out today:

### VMFrame (20 bytes + the inline arguments, locals, and stack)

| offset | field | how hot |
|---|---|---|
| 4 | `bytecodeIndex` | only at frame switches and GC |
| 6 | `totalObjectSize` | the GC only |
| 8 | `previousFrame` | returns |
| 10 | `context` | blocks, non-local returns, `GetOuterContext()` |
| 12 | `method` | frame switches, `GetSelf()` paths |
| 14 | `arguments` | push/pop argument |
| 16 | `locals` | push/pop local, the most common bytecodes |
| 18 | `stack_ptr` | **every push and pop** |

The hottest field is last, and the short slots hold the GC's size and the
saved program counter, which the interpreter rarely touches.

### VMMethod (24 bytes + constants and bytecodes)

| offset | field | how hot |
|---|---|---|
| 4 | `hash` (`VMInvokable`) | only `Object>>hashcode` |
| 6 | `signature` (`VMInvokable`) | method lookup |
| 8 | `holder` (`VMInvokable`) | super sends, `SetHolder` |
| 10 | `numberOfLocals` | `NewFrame` |
| 12 | `maximumNumberOfStackElements` | `NewFrame` |
| 14 | `bcLength` | rarely |
| 16 | `numberOfArguments` (`uint8_t`) | every send and return |
| 18 | `numberOfConstants` | rarely |
| 20 | `indexableFields` | **`GetConstant`: push constant, push global, every send** |
| 22 | `bytecodes` | every frame switch |

## The idea

### VMFrame

Put the stack and variable pointers into the short slots:

```
4  stack_ptr
6  locals
8  arguments
10 method
12 context
14 previousFrame
16 bytecodeIndex
18 totalObjectSize
```

Field order in C++ is declaration order, so this is a reordering of the
declarations in `VMFrame.h`, plus the constructor's initializer list (which
GCC warns about if it doesn't match). Check `VMFrame::CloneForMovingGC`, the
emergency frame copy, and anything else that copies by offset.

### VMMethod

Here the short slots belong to the base class `VMInvokable`, so reordering
`VMMethod` alone can't reach them. Options, roughly in order of effort:

1. **Drop `hash` from `VMInvokable`** (and `VMObject`): it only serves
   `Object>>hashcode` and could be derived from the address with a non-moving
   GC, or from the signature. That frees a short slot in every invokable, and
   2 bytes per object.
2. **Reorder `VMInvokable`** so `signature` is first, then `holder`.
3. **Don't store `indexableFields` and `bytecodes` at all.** They point into
   the method object itself, at a constant offset (`indexableFields` is
   `&indexableFields + 2`, `bytecodes` follows the constants), so they can be
   computed from `this` and `numberOfConstants`. That saves 4 bytes per method
   and turns two loads into an add. The same is true of `VMString::chars`,
   which points just behind itself in every string and symbol.
4. If the fields stay, put `indexableFields` (or `numberOfConstants`, if the
   constants pointer is computed) into a short slot, since `GetConstant` runs
   for every send, constant, and global.

### Byte fields at offsets 1 and 3

`numberOfArguments` is a byte, and byte loads also have short forms at 1 and
3. Those are inside the vtable pointer and `gcField` today. With the
mark-sweep GC that fructus uses, `gcField` only needs a mark bit, so it could
become a byte at 2, leaving 3 for a byte field such as
`numberOfArguments`. The other GCs store forwarding pointers there, so this
would be fructus-only, and `VMFreeListEntry` mirrors the layout.

## How to measure

The interpreter's code is 3809 bytes of `.text` after it became an object.
Compare per-object sizes in `build-fructus/SOM++.map` before and after; the
offsets above were obtained by compiling `offsetof()` expressions with
`fructus-elf-g++`.
