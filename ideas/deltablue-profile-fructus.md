# Profile of DeltaBlue on fructus (-mmul)

Not an idea as such: a record of where the cycles went, as a starting point
for speeding up the fructus interpreter.

## Cycle counts per commit

We want to track the DeltaBlue and Richards cycle counts on every commit
that changes them: add a row here with the commit that changed them.

Both are the cut-down versions (`Examples-fructus/AreWeFastYet/RunDeltaBlue.som
1` and `RunRichards.som 1`), MinSizeRel, `GC_TYPE=SLIDING`, `-mmul`, in the
build dirs `build-fructus-awfy-mmul` and `build-fructus-richards-mmul`; the
Richards one has the same settings as the DeltaBlue one except
`FRUCTUS_PROGRAM`. The numbers are from
`node ~/fructus/tools/fcc-run.mjs <build>/SOM++.bin --stats --max 2e9`, which
agrees exactly with `~/fructus/tools/profile.mjs <build>/SOM++`.

| commit | DeltaBlue cycles | DeltaBlue insns | Richards cycles | Richards insns | note |
|---|---:|---:|---:|---:|---|
| a73b63d6 | 5,089,608 | 1,671,744 | | | Before classIds |
| ad04a24d | 4,478,235 | 1,487,094 | 136,266,060 | 44,611,969 | Dispatch by classId ranges and rings |

## Setup

- Build dir `build-fructus-awfy-mmul`: the settings of `build-fructus-awfy`
  (MinSizeRel, `GC_TYPE=SLIDING`, tagging, the AreWeFastYet class path) plus
  `-DCMAKE_CXX_FLAGS=-mmul -DCMAKE_EXE_LINKER_FLAGS=-mmul`.
- Program: `Examples-fructus/AreWeFastYet/RunDeltaBlue.som 1`, the cut-down
  DeltaBlue, not AWFY size.
- Source at commit a73b63d6 ("Fix link"), 2026-10-11.
- The binary has 58 `mul` instructions. `SOM++.bin` is 65,522 bytes, 14
  bytes short of the 64K address space.
- Run with `node ~/fructus/tools/fcc-run.mjs build-fructus-awfy-mmul/SOM++.bin --stats`.

Result: `DeltaBlue (CUT DOWN, NOT AWFY SIZE): ok`, **1,671,744 instructions,
5,089,608 cycles**, 3.04 cycles per instruction. About 28K bytecodes ran, so
roughly 180 cycles per bytecode.

There's no comparison with a build without `-mmul`: `build-fructus-awfy` was
stale (built 2026-10-08) and never finished. It ran 2e9 instructions at
exactly 6 cycles each and stopped at pc 0xfff8, which looks like an exception
loop.

## Method

Two small scripts on `tools/sim.js` stepped the machine one instruction at a
time and charged `m.cyc` deltas:

- **Per function**: each instruction's cycles go to the function its pc is in
  (nm text symbols), for self time. A shadow call stack gives inclusive time:
  push on `lr = pc; pc = ...` instructions, pop when the pc reaches a
  return address on the stack. A function reached by a tail jump gets no
  inclusive time of its own; its time is counted in whatever called the
  function that jumped to it.
- **Per bytecode**: each dispatch in `Interpreter::Start` (`ld r0, [r6, r0]`,
  opcode = r0/2) starts a new bucket, so a bytecode includes everything it
  calls, including any GC it triggers.
- **Send misses**: `(bytecode pointer, class)` at the entry of
  `Interpreter::sendMiss`, which is r1 and r2 since r0 is `this`.

## By bytecode

| bytecode | % of cycles | count | cycles each |
|---|---:|---:|---:|
| SEND | 42.7% | 4022 | 541 |
| SEND_1 | 23.6% | 2406 | 499 |
| RETURN_LOCAL | 4.5% | 1619 | 142 |
| PUSH_FIELD | 2.8% | 1273 | 113 |
| RETURN_SELF | 2.8% | 1052 | 135 |
| PUSH_BLOCK | 2.3% | 272 | 425 |
| PUSH_ARG_1 | 2.1% | 2141 | 49 |
| PUSH_FIELD_1 | 2.0% | 955 | 109 |
| PUSH_GLOBAL | 1.8% | 216 | 430 |
| PUSH_SELF | 1.3% | 1313 | 49 |
| JUMP_IF_GREATER | 1.2% | 794 | 80 |
| PUSH_FIELD_0 | 1.2% | 582 | 108 |
| PUSH_LOCAL_0 | 1.2% | 1138 | 53 |
| POP | 1.1% | 2063 | 28 |
| INC | 1.0% | 700 | 70 |
| SUPER_SEND | 0.6% | 74 | 405 |
| everything else | ~8% | | 18–80 |

Sends account for about two thirds of the cycles.

## By function

Self time (the instructions of the function itself):

| % | function | calls |
|---:|---|---:|
| 26.5% | `Interpreter::Start()` | 2 |
| 8.9% | `Universe::NewFrame` | 2675 |
| 6.5% | `VMClass::LookupInvokable` | 879 |
| 6.4% | `Interpreter::doSend` | 4022 |
| 5.7% | `VMClass::GetInstanceInvokable` | 9071 |
| 4.4% | `Interpreter::popFrameAndPushResult` | 2671 |
| 4.0% | `Interpreter::doPushFieldWithIndex` | 2810 |
| 3.6% | `Interpreter::doUnarySend` | 2406 |
| 3.5% | `VMFrame::CopyArgumentsFrom` | 1624 |
| 3.3% | `SlidingHeap::AllocateObject` | 3126 |
| 2.0% | `Interpreter::PushNewFrame` | 2675 |
| 2.0% | `VMSafeBinaryPrimitive::Invoke` | (tail jumps) |
| 1.7% | `Interpreter::sendMiss` | 877 |
| 1.5% | `FructusImage::GetGlobal` | (tail jumps) |

Inclusive (including callees):

| % | function |
|---:|---|
| 34.6% | `Interpreter::doSend` |
| 18.8% | `Interpreter::sendMiss` |
| 17.7% | `Interpreter::doUnarySend` |
| 13.8% | `Interpreter::PushNewFrame` |
| 12.2% | `VMClass::LookupInvokable` |
| 11.8% | `Universe::NewFrame` |
| 5.3% | `SlidingCollector::Collect` (2 collections) |
| 4.8% | `Interpreter::doPushFieldWithIndex` |
| 4.6% | `Interpreter::popFrameAndPushResult` |
| 3.6% | `VMFrame::CopyArgumentsFrom` |
| 3.4% | `SlidingHeap::AllocateObject` |
| 2.0% | `Interpreter::doPushBlock` |

## Findings

### Send misses and lookup: 18.8%

877 of the 6,500 sends miss the inline cache (see `SendSite.h`: monomorphic,
and a send to another class looks the selector up every time without
relinking). Each miss costs about 1,090 cycles.

- 390 misses are first-time linking at distinct sites. The cut-down
  benchmark executes most send sites only a few times, so linking weighs
  more here than it would in a longer run.
- 410 are repeat misses at a site for a class it has already missed with.
  These are polymorphic sites. The worst:
  - `c execute` in `Plan>>execute` (Plan.som:17): EqualityConstraint 100
    times, ScaleConstraint 40.
  - `c isSatisfied` and friends in
    `Planner>>addConstraintsConsuming:to:` (Planner.som:126): ScaleConstraint,
    StayConstraint, EditConstraint.
  - `current notNil` in `SomDictionary>>keys` (SomDictionary.som:166): Nil.
  - In `AbstractConstraint>>satisfy:propagate:` (AbstractConstraint.som:123):
    StayConstraint.

The lookup is a linear search of each class's invokables and then its
superclasses (`VMClass.cpp:202`): 9,071 `GetInstanceInvokable` calls for 879
lookups, about 10 methods checked per lookup. `GetInstanceInvokable`
(`VMClass.cpp:186`) isn't inlined at -Os and does a bounds check, so each
method checked costs about 68 cycles including the loop in
`LookupInvokable`.

Possible fixes: a small polymorphic inline cache (two entries would cover
`Plan>>execute`), or a global lookup cache keyed on (class, selector); and
inlining the invokable access in the lookup loop without the bounds check.

### Frame creation: 13.8%

`PushNewFrame` → `Universe::NewFrame` costs about 260 cycles per frame
(`NewFrame` alone 225):

- `SlidingHeap::AllocateObject`, 3.4% (about 56 cycles per allocation over
  all 3,126 allocations).
- The loop filling the new frame with nil, 9,715 slots at about 13 cycles
  each: 2.4%.
- The rest of `NewFrame` itself, computing the size, reading the heap
  pointer and setting up the header and frame fields: about 125 cycles.

`VMFrame::CopyArgumentsFrom` (3.6%, 109 cycles per call) does a write barrier
(the `st8` card mark at `0x200 + (frame >> 7)`) for every argument it copies
into a frame that was just allocated. It also re-reads the caller's stack
pointer at every iteration. The arguments are nil-filled by `NewFrame` and
then overwritten, so those slots are written twice.

`popFrameAndPushResult` is another 4.6%, about 87 cycles per return.

### Dispatch: about 7%

Each dispatch is `ld8`, `shl`, `ld`, `jmp` at 4 + 1 + 4 + 4 = 13 cycles, so
about 364K cycles for 28K bytecodes. `Interpreter::Start` also copies its
130-byte jump table onto the stack when it starts (2 calls, negligible here).

### Pushes and the write barrier

Each push of a heap pointer onto a frame's stack, in the handlers in `Start`
and in `doPushFieldWithIndex`, marks a card, because frames are heap
objects. That is a 5-cycle `st8` plus the test of the tag on every push.

`doPushFieldWithIndex` (about 87 cycles per call) calls
`VMFrame::GetOuterContext()` every time, which is a call and at least one
loop iteration even in a method, where the frame is its own outer context.

### GC: 5.3%

Two full collections by the sliding collector, about 135K cycles each:
marking about 52K, sliding about 59K, counting live granules about 9K, and
walking the roots about 11K.

### Other

- `PUSH_BLOCK` costs 425 cycles: `NewBlock` allocates, nil-fills, and calls
  through `GetBlockClassWithArgs` → `FructusImage::BlockClass`.
- `PUSH_GLOBAL` costs 430 cycles, almost all in `Universe::GetGlobal` and
  `FructusImage::GetGlobal` (about 350 cycles per lookup).
- `mul` doesn't show up as hot anywhere. The one in `CopyArgumentsFrom` is a
  multiply by -2.

## Profile at ad04a24d (classId dispatch)

After the commits that gave classes a classId and dispatch by classId ranges
and rings of invokables (15e27e5a..ad04a24d), 2026-10-11. Same build settings
as above, rebuilt at ad04a24d (`SOM++.bin` 64,374 bytes, 1,162 bytes smaller).
Profiled with `node ~/fructus/tools/profile.mjs --kinds
build-fructus-awfy-mmul/SOM++`, which charges the simulator's `cost()` per
instruction by function. It has no per-bytecode table.

Result: **4,478,235 cycles, 1,487,094 instructions**, 3.01 cycles per
instruction: 12% fewer cycles than at a73b63d6, about 160 cycles per
bytecode instead of 180.

### Where the gain came from

`VMClass::LookupInvokable` and `GetInstanceInvokable` (12.2%, about 620K
cycles) are gone. `sendMiss` now does the lookup itself, at 139K cycles self
time (3.1%). That is nearly the whole 611K cycles saved. `doSend` went from
about 326K to 287K; the other large functions are within a few percent of
before.

### By function (self time)

| % | cycles | function |
|---:|---:|---|
| 30.7 | 1,373,073 | `Interpreter::Start` |
| 10.1 | 452,511 | `Universe::NewFrame` |
| 6.4 | 287,278 | `Interpreter::doSend` |
| 5.0 | 223,848 | `Interpreter::popFrameAndPushResult` |
| 4.6 | 205,460 | `Interpreter::doPushFieldWithIndex` |
| 4.3 | 190,551 | `Interpreter::doUnarySend` |
| 4.1 | 182,003 | `VMFrame::CopyArgumentsFrom` |
| 3.7 | 165,620 | `SlidingHeap::AllocateObject` |
| 3.1 | 138,555 | `Interpreter::sendMiss` |
| 2.3 | 101,628 | `Interpreter::PushNewFrame` |
| 1.9 | 86,724 | `VMSafeBinaryPrimitive::Invoke` |
| 1.7 | 74,968 | `FructusImage::GetGlobal` |
| 1.5 | 66,614 | `VMMethod::Invoke` |
| 1.2 | 55,698 | `mark_object` |
| 1.2 | 54,713 | `update_pointer` |
| 1.1 | 49,235 | `VMMethod::Invoke1` |
| 1.0 | 45,657 | `VMFrame::SetTop` |
| 1.0 | 44,576 | `VMObject::WalkObjects` |
| 0.9 | 40,608 | `VMGetter::Invoke1` |
| 0.9 | 39,620 | `VMSetter::Invoke` |
| 0.8 | 37,257 | `VMFrame::GetOuterContext` |
| 0.8 | 37,129 | `VMFrame::Push` |
| 0.8 | 34,146 | `VMObject::nilInitializeFields` |

### By kind of cost

| kind | % | CPI |
|---|---:|---:|
| load | 42.7 | 4.10 |
| store | 24.1 | 5.21 |
| jump, call, ret | 13.0 | 3.48 |
| alu | 10.4 | 1.00 |
| branch taken | 5.5 | 4.00 |
| branch not taken | 2.4 | 1.42 |
| alu, odd 3-byte | 1.4 | 2.00 |
| mul | 0.5 | 2.00 |

`push` and `pop`, mostly saving and restoring registers in prologues and
epilogues, are 10.2% between them.

### What stands out now

- Frames are the largest target: `NewFrame`, `popFrameAndPushResult`,
  `CopyArgumentsFrom`, `PushNewFrame` and `AllocateObject` (mostly frames)
  add up to about 25%, unchanged since a73b63d6. The findings under "Frame
  creation" above still apply.
- Calls cost 13%, and many small helpers aren't inlined: `GetOuterContext`,
  `SetTop`, `VMFrame::Push`, `nilInitializeFields`.
- GC is about 3.5% (`mark_object`, `update_pointer`, `WalkObjects` and the
  rest).
