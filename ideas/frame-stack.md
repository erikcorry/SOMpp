# Allocate most frames on a stack instead of the heap

## Background

Every send to a method or block allocates a `VMFrame` in the heap
(`Universe::NewFrame()`). Its arguments, locals and operand stack are inline
after the header, so a frame is one object of 18 bytes plus 2 for each slot.
Frames are only freed by a later collection.

On fructus the heap is the mark-sweep heap, with an unsorted first-fit free
list. A per-function profile of DeltaBlue in the simulator shows allocation
costing more than the interpreter loop:

| function | cycles | share |
|---|---:|---:|
| `MarkSweepHeap::findFittingEntry` | 1.97M | 26% |
| `Interpreter::Start` | 1.42M | 19% |
| `mark_object` | 0.50M | 7% |
| `Universe::NewFrame` | 0.43M | 6% |
| `MarkSweepHeap::AllocateObject` | 0.31M | 4% |
| `VMObject::WalkObjects` + `sweep` + `markReachableObjects` | 0.56M | 7% |

(7.51M cycles in all, at commit 497f1414.) The free-list search is also
sensitive to the sizes of objects: making frames 2 bytes smaller moved
DeltaBlue's total by +2.7%, all of it in `findFittingEntry`, while Richards
got 1% faster. Most of what is allocated, and most of what makes the free
list long, is frames.

## Which frames can go on a stack: methods that don't capture their frame

A frame can be referenced after its call returns only if a block captured it
as its context. A block's context is the frame that executed its
`PUSH_BLOCK`. So a method or block method whose bytecodes contain no
`PUSH_BLOCK` (blocks the compiler inlined, like those of `ifTrue:` and
`whileTrue:`, don't count, since they aren't there as `PUSH_BLOCK`) can
never have its frame referenced once it returns.

Such frames are created and dropped in strict LIFO order, even when they
send, since their callees' frames are above them, so they can be allocated
on a frame stack.

Measured with a temporary counter in `Universe::NewFrame()` on the host, with
the fructus programs (`RunDeltaBlue.som 1`, `RunRichards.som 1`):

| program | frames | without `PUSH_BLOCK` | strict leaves (no sends either) |
|---|---:|---:|---:|
| DeltaBlue | 2,510 | 2,179 (87%) | 204 (8%) |
| Richards | 50,815 | 50,798 (99.97%) | 2,374 (4.7%) |

The frames left in the heap are those of methods like `Plan>>execute`,
`Vector>>append:` and `Planner>>addConstraintsConsuming:to:`, which pass
blocks to `do:` and the like.

Trivial getters, setters and literal returns (`VMGetter`, `VMSetter`,
`VMLiteralReturn`, `VMGlobalReturn`) already have no frame: they work on the
caller's stack.

### The simpler variant: one static frame for strict leaves

A method with no sends and no `PUSH_BLOCK` can't be re-entered while it runs,
so all of them can share one static frame, sized for the largest of them.
That is simpler (no stack pointer, no overflow check), but covers only 5–8%
of the frames, against 87–100% for the stack.

## Design

- **Which frame kind a method uses.** A per-method flag, "has no
  `PUSH_BLOCK`". On fructus the image writer knows it and can put it in a
  spare bit, e.g. in `numberOfLocals` or `maximumNumberOfStackElements`, or
  give such methods their own vtable so `Invoke` needs no test. On the host
  the compiler can compute it in `MethodGenerationContext::Assemble()`.
- **The stack.** A fixed area, `FRUCTUS_FRAME_STACK_SIZE`, outside the heap.
  `NewFrame` bumps a stack top for flagged methods and checks for overflow,
  which is an error like running out of heap.
- **Popping.** `popFrame()` already clears the returning frame's
  `previousFrame`. For a stack frame it also moves the stack top back to the
  frame's start. Frames are popped one at a time, also when a non-local
  return unwinds (`doReturnNonLocal()` calls `popFrame()` until it reaches
  the home context), so the stack top always follows.
- **Heap frames pointing at stack frames.** A heap frame's `previousFrame`
  can be a stack frame, but only while the heap frame is active, which is
  while its caller is. When it returns, `popFrame()` clears it, which is also
  what makes a block whose home has returned an escaped block. Block
  contexts always point at heap frames, since only methods with
  `PUSH_BLOCK` make blocks.
- **Stack frames pointing at heap frames** (a block's own frame, whose
  `context` is its home) are ordinary pointers.

### GC

- **Mark-sweep (now).** Stack frames are reached through the interpreter's
  frame and the `previousFrame` chain, and marked like other objects outside
  the heap (`MarkSweepHeap::Contains()` keeps them out of the live bytes and
  the sweep). Dead frames above the stack top are never reached.
- **Moving (planned).** Stack frames are roots that don't move: the GC walks
  their slots and `context` in place, and never copies them. Their
  `locals` and `stack_ptr` stay valid, since the frame doesn't move. The
  `CloneForMovingGC()` path is only for heap frames.
- A safe point only happens at bytecode boundaries (`DISPATCH_GC`), where
  every live frame is on the chain, so nothing else needs rooting.

### Things to check

- **`VMFrame::EmergencyFrameFrom()`** replaces the current frame with a bigger
  copy (for `#escapedBlock:`, and `#doesNotUnderstand:` /
  `#unknownGlobal:` on the host). For a stack frame, the copy can't go in
  the heap if anything above could point at it; it's always the top frame
  there, so it can be grown in place on the stack instead, or the copy can
  go on the stack.
- **The bootstrap frame** and frames made from C++ (`VMObject::Send()`) go
  through `NewFrame` too.
- **Stack traces** walk `previousFrame` and are unaffected.
- **The host.** Using the frame stack on the host as well keeps one code
  path. It needs a bigger stack or growing, since host programs recurse
  deeper. Or the host keeps heap frames behind a compile-time switch.
- **Frame stack size.** Recursion depth times frame size. Richards and
  DeltaBlue are shallow, but a deep recursion that fit in the heap before
  could now overflow the stack sooner, or later, depending on the split
  between the heap and the stack in fructus' 64K.

## Variant: the stack as a heap object, without a write barrier

Since the above was written, fructus moved to the sliding heap
(`SlidingHeap.h`): bump allocation, generational with instant promotion,
cards at `0x200` as the remembered set. Allocation is no longer the free-list
search; in the profile at ad04a24d (`ideas/deltablue-profile-fructus.md`) the
frame work (`NewFrame`, `popFrameAndPushResult`, `CopyArgumentsFrom`,
`PushNewFrame`, most of `AllocateObject`) is about 25% of DeltaBlue, and every
push of a pointer onto a frame's operand stack also pays the card mark.

The idea: the frames of methods without `PUSH_BLOCK` go on a Smalltalk stack
that is itself one heap object, which can be grown on overflow. Stores into
it skip the write barrier; instead, just before each minor GC, the live part
of the stack is explicitly added to the remembered set.

### Why the stack doesn't need the barrier

Only the top frame of the stack is ever written. Stack frames have no blocks,
so no block's outer-context store (`POP_LOCAL`/`POP_ARGUMENT` with a context
level above 0) can reach one: those always go to heap frames. So the stores
are the operand stack pushes of the current frame, its locals, the arguments
of a new frame, and the result pushed into the caller on return.

### Generalising: no barrier on any store into the current frame

To skip the barrier only for stack frames, each handler would have to test
the frame kind, which costs about what the barrier does. Instead, skip it
for every store into the current frame, heap frame or not:

- At a minor GC, scan every frame on the active `previousFrame` chain as a
  root, heap frames included.
- When a heap frame is popped, mark its card. That's the only way an old
  heap frame holding young pointers stored without a barrier can stay alive
  off the chain, through a block that captured it. One `st8` per heap-frame
  return, and heap frames are 13% of DeltaBlue's frames and 0.03% of
  Richards'.
- Stores through an outer context (context level above 0) keep the barrier.
- A newly allocated frame is young, so the stores that fill it never needed
  the barrier.

### What "added to the remembered set" has to mean

Marking the stack's cards dirty and letting the card scan walk it as an
ordinary object would be wrong: above the top frame, and above each frame's
`stack_ptr`, are stale slots that may point at young objects that have since
died and been slid over. So the stack needs its own walk at a minor GC:
frame by frame from the bottom (or the watermark, below) to the top, and
in each frame only up to `stack_ptr`, as `VMFrame::WalkObjects()` already
does.

**A watermark.** A minor GC leaves no young objects, and only the top frame
is ever written, so a frame that has stayed below the top since the last
minor GC can't hold young pointers. Keeping the lowest stack top since the
last minor GC (a compare in `popFrame()`) lets the minor GC scan only from
there up. For a deep stack in a short-running loop, that's most of it.

### Further things it enables

- **No argument copy.** On the stack, the callee's arguments can be the
  receiver and arguments that the caller pushed, in place: the callee's
  `arguments` points back into the caller's operand stack and its header
  goes after them. That removes `CopyArgumentsFrom` (4.1%) and the caller's
  pop of the arguments, and a return is resetting the stack top to the
  receiver's slot and storing the result there. This only works for a stack
  callee; a heap frame still copies.
- **Less nil-filling.** The `VMFrame` constructor fills the locals *and*
  the operand stack area with nil, though the GC walk stops at `stack_ptr`.
  Only the locals need it, which would help heap frames too, unless
  something relies on it (to check: `EmergencyFrameFrom()`).
- Fewer young objects, so fewer minor GCs.

### Moving and growing the stack

- The sliding collector already moves pointers into the middle of a frame
  (`locals`, `stack_ptr`), because a live object's whole extent is marked, so
  pointers into the middle of the stack object (a stack frame's
  `previousFrame`, `locals`, `stack_ptr`, and a heap frame's `previousFrame`
  that points at a stack frame) would be relocated the same way. But
  marking must not take a pointer to a stack frame for an object start: it
  has to recognise the stack's range and skip it, since the stack is a root
  walked by frames.
- Allocated first, the stack is the oldest object, at the top of the heap,
  and a slide never moves it until it grows.
- Growing copies it into a bigger object and relocates every pointer into
  it: within it, in the heap frames on the active chain, the interpreter's
  frame, and its cached `locals`/stack pointer in `Interpreter::Start`.
  Storing frame links within the stack as offsets would shrink that list.
  C++ code holding a `VMFrame*` across a `NewFrame` call has the same
  problem as with the moving GC; growing only from within `NewFrame` in the
  send path, where the interpreter reloads its state afterwards, keeps it to
  one place.

## Related

- The frame header still has `method` and `bytecodeIndex`. Storing the pc
  instead of the index would remove most uses of `method` (pc reloads, stack
  traces); returns could get the argument count from `locals` minus the
  start of the arguments. `Block>>restart` is the main other user.
- Size-segregated free lists for the common object sizes would help the
  remaining heap allocations, until the GC is moving and allocation is a
  bump.
