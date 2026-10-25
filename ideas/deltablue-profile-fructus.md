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

The 25d7053b rows are one binary, built with fructus af28add's toolchain,
run on the `tools/sim.js` of each fructus commit with af28add's
`isa/fructus.toml`: the simulator takes its opcodes from the spec, so an
older timing model runs today's encoding. The first of them is the code
change alone, on the timing of ebd3f71; each after it is one CPU change.
From the 5b70889 row on, the binary is rebuilt with fructus 5b70889's
toolchain and run with today's `isa/fructus.toml` (b23ac26 moved the
offset-0 loads); that row is the new code on 9f7d80e's timing, the
compiler's change alone. The f840fa3 row is that binary on f840fa3's
`tools/sim.js` and `isa/fructus.toml`, the last spec with `recip`, which
its libgcc still runs. From the 0aa5f42 row on, the binary is rebuilt with
fructus 354eb00's toolchain, whose libgcc makes the reciprocal itself.

| commit | DeltaBlue cycles | DeltaBlue insns | Richards cycles | Richards insns | note |
|---|---:|---:|---:|---:|---|
| a73b63d6 | 5,089,608 | 1,671,744 | | | Before classIds |
| ad04a24d | 4,478,235 | 1,487,094 | 136,266,060 | 44,611,969 | Dispatch by classId ranges and rings |
| 26793a87 | 4,477,951 | 1,487,043 | 136,266,059 | 44,611,968 | Rebuilt with the new fructus gcc and assembler |
| 0442f246 | 4,481,226 | 1,487,137 | 136,772,794 | 44,726,494 | Arguments and locals relative to the stack top |
| 77938552 | 4,339,089 | 1,455,568 | 131,045,100 | 43,328,009 | Stack pointer in a register |
| 9180e1de | 4,021,821 | 1,330,370 | 122,015,474 | 39,605,666 | Remember the current frame, no barrier on its stack |
| e62e7f7d | 3,875,198 | 1,270,388 | 115,499,358 | 37,199,502 | No barrier on arguments and primitive results |
| cde6bc72 | 3,422,459 | 1,270,388 | 106,614,706 | 37,199,484 | fructus 0331767: instruction buffer and fetcher, deeper pipeline |
| 347beb0f | 3,415,091 | 1,270,388 | 107,094,827 | 37,199,484 | fructus 9e49ec9: 32 one-byte opcodes; code 3.0% smaller, same instructions, cycles move with alignment |
| f2571497 | 3,415,058 | 1,270,363 | 107,094,298 | 37,199,211 | fructus 6b4c7f0: ishi, branchless carry; 60 uses, a few hundred cycles |
| 25d7053b | 3,391,808 | 1,262,443 | 106,085,921 | 36,873,606 | fructus ebd3f71's timing, af28add's code: no jmpr, push order, gcc's xor-less inverted compares, the opcode moves; core-lib e362972 |
| 25d7053b | 3,276,532 | 1,262,443 | 101,546,269 | 36,873,606 | fructus c51921e: jmp and call to an absolute address redirect as they dispatch |
| 25d7053b | 3,302,689 | 1,262,443 | 102,143,400 | 36,873,606 | fructus 76d6d4f: predicted call 3 cycles, not 2, for 94 fewer LUT4 |
| 25d7053b | 3,333,844 | 1,262,443 | 102,984,371 | 36,873,606 | fructus 3893847: backward branches predicted taken; DeltaBlue's are taken 36% |
| 25d7053b | 3,243,003 | 1,262,443 | 97,185,166 | 36,873,606 | fructus d170fdf: dynamic branch predictor, 11 bits of history |
| 25d7053b | 3,229,571 | 1,262,443 | 96,772,017 | 36,873,606 | fructus ae14399: every jump puts a bit of its address in the history |
| 25d7053b | 3,146,423 | 1,262,443 | 93,871,036 | 36,873,606 | fructus af28add: jmp ra and call ra guessed from a table of where they went |
| 25d7053b | 3,059,012 | 1,262,443 | 91,729,387 | 36,873,606 | fructus ec0304c: ret guessed from a return stack |
| 25d7053b | 2,988,520 | 1,262,443 | 88,092,692 | 36,873,606 | fructus 5f0f2af: a branch guessed taken reads its target as it dispatches, two cycles |
| 25d7053b | 2,820,542 | 1,262,443 | 84,199,414 | 36,873,606 | fructus 9f7d80e: a store of one register does not stop dispatch, N + 1 cycles |
| 25d7053b | 2,746,797 | 1,239,431 | 80,378,320 | 35,710,882 | fructus 5b70889's gcc on 9f7d80e's timing: -Os rotates loops, copies short returns, skips the high word of a 32-bit x++ |
| 25d7053b | 2,745,087 | 1,239,431 | 80,273,318 | 35,710,882 | fructus 6c667ac: ret, jmp ra and call ra guessed once their own bytes are in |
| 25d7053b | 2,484,106 | 1,239,431 | 72,704,462 | 35,710,882 | fructus 68fad41: ld and ld8 take two cycles, written as their word arrives |
| 25d7053b | 2,547,460 | 1,239,431 | 74,898,440 | 35,710,882 | fructus f840fa3: no jmp ra table, only ret guessed - 354 cells the system with UART and USB needed |
| 25d7053b | 2,548,788 | 1,240,092 | 74,860,004 | 35,691,432 | fructus 0aa5f42: no recip, libgcc's __recip_mul instead; rebuilt with 354eb00's toolchain, putchar reading the UART through the late bit's window |
| 6bfa2245 | 2,258,073 | 1,065,566 | 66,074,258 | 31,019,914 | Frames of methods without blocks on a frame stack; DeltaBlue no longer collects |
| 67f69752 | 2,232,886 | 1,049,571 | 65,615,047 | 30,713,392 | No write barrier on the C++ stores into the current frame (8a978552), no totalObjectSize in fructus's frames (639ab731), the frame stack grown by just what a frame needs |
| f7672260 | 2,243,428 | 1,056,682 | 65,774,494 | 30,843,925 | Frames split into VMStackFrame and VMHeapFrame; NewFrame has a construction site per kind |
| 8016517d | 2,159,186 | 1,031,028 | 63,562,606 | 29,926,830 | sp passed into Invoke: a send saves and reloads only the pc |
| b4cfc11a | 2,173,670 | 1,033,275 | 61,672,016 | 28,976,132 | No stack_ptr in stack frames; self in the interpreter, saved above a send's arguments |
| 24d7e292 | 1,964,887 | 942,597 | 56,845,447 | 26,611,773 | Frames pushed and popped inline in Start(), with bytecodes for stack methods' sends and returns; stack frames keep their arguments in the sender's stack |
| f60ee0ff | 1,943,665 | 941,590 | 55,951,702 | 26,568,111 | fructus 0ab3ed0: gcc sends a far branch to a nearby jmp, not over one; INC's fast path takes no branch |
| 370470b5 | 1,942,377 | 941,590 | 55,951,910 | 26,568,111 | checkIsGreater expects two small integers |

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
