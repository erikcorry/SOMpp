# Allocate most frames on a stack instead of the heap

The design that was chosen is in "Chosen design: a stack below the heap"
at the end. The sections before it are the exploration that led there.

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

## Chosen design: a stack below the heap

Decided 2026-10-24, for the sliding heap, on fructus and on the host.

### Which functions use the stack

- The compiler marks every method and block method as a stack function or
  a frame function, with a flag in the method object (not a vtable of its
  own). A stack function has no `PUSH_BLOCK` in its bytecodes; inlined
  blocks don't count. So a block can be a stack function too, if it has no
  blocks in it.
- The method also records the stack space its frame needs at most, the
  high-water mark: the frame header, the arguments, the locals and the
  maximum stack depth. Every function's requirement includes
  `EXTRA_STACK_SLOTS`, the slots for the sends the VM makes that the
  compiler doesn't count (#doesNotUnderstand:arguments:, #escapedBlock:,
  #unknownGlobal:, the Vector primitives' #error:). Those are 0 on fructus,
  which makes none of them, and frames never grow on either.

### The stack

- It grows up from the bottom of the heap's memory, and the heap allocates
  down from its top towards it. The stack is like one huge frame object,
  with an end that is bumped when it needs more room, and never shrunk (for
  now).
- The bump allocator checks against the stack's end instead of the bottom
  of memory. On fructus the stack's end is the heap's `bottom`. On the host
  the stack is at the base of the reservation, the heap's accessible part
  grows down towards it, and the stack's pages are made accessible as its
  end moves up.
- On entry to a stack function, the interpreter reads the function's
  high-water mark, and checks that it fits between the stack top and the
  stack's end. If it doesn't, the end is bumped into the free space, if
  there's enough; otherwise it's GC time. The send fails without pushing a
  frame and is retried after the collection, like a failed allocation. If
  there's still no room after that, it's out of memory.
- The high-water mark is only the check. On an actual call, the new frame
  goes wherever the stack top is, which is right above the caller's
  `stack_ptr` if the caller is a stack frame, not above the caller's
  high-water mark. So the live frames are contiguous, with no gaps.
- **The stack top while a heap frame is current.** When the current frame
  is a stack frame, the top is its `stack_ptr + 1`. When it's a heap frame,
  the top is the interpreter's `stackTop` field. It's set to the caller's
  `stack_ptr + 1` when a stack frame calls a frame function, and to the
  returning frame's start when a stack frame returns into a heap frame.
  That covers stack → heap → stack chains: a stack method sends to a
  method with blocks, which sends to a stack method.
- Whether a frame is a stack frame is an address compare against the
  stack's end. `popFrame()` uses it to move the top back, and `SetFrame()`
  uses it to skip `remember_object` for stack frames.

### The frames on the stack

- A stack frame keeps a header with the links that returns need. The GC's
  scan doesn't need frame boundaries, but unwinding does, and the flat
  stack has no other structure to find them by. An ordinary return needs
  `previousFrame` to know which frame becomes current, and a stack frame's
  caller can be a heap frame, so it can't be found from the stack's layout.
  A non-local return (`doReturnNonLocal()`) follows the same chain with
  `popFrame()` until it reaches the home, which is always a heap frame,
  since it makes blocks. Popping a stack frame on the way only moves the
  stack top back. Popping a heap frame clears its `previousFrame`, as now,
  which is what makes the blocks whose home has returned escaped. For
  example, in `H (heap) → S2 (stack) → B`, `B` is popped, `S2` returns into
  `H` and sets `stackTop` to its own start, and the final
  `popFrameAndPushResult()` returns from `H` as usual. The least a stack
  frame needs is `previousFrame`, the method and the bytecode pointer to
  resume it, `locals`, and `stack_ptr` for its top while it isn't current.
  Block frames also need `context`.
- No frame has `totalObjectSize` any more, so heap and stack frames keep
  one layout. A stack frame doesn't need it: `GetObjectSize()` doesn't
  apply to it, since the heap never walks it, and its extent ends at its
  `stack_ptr`, where the next frame starts. A heap frame's
  `GetObjectSize()` computes its size from its method instead: the
  arguments, the locals, the maximum stack depth and `EXTRA_STACK_SLOTS`,
  the same sum `Universe::NewFrame` makes.
- Only the locals are filled with nil. Nothing beyond the stack pointer is
  a root, so the operand stack area needs no filling. Heap frames can do the
  same, since `VMFrame::WalkObjects()` stops at `stack_ptr` too.
- On fructus, `bytecodeIndex` becomes a bytecode pointer. It's above
  0x8000, so it looks either like a negative Smi or like a pointer out of
  the heap, both of which the GC skips. It also saves the index-to-pc
  conversion when the pc is saved and reloaded.

### GC

- The stack is always a root, and every collection, minor or major, scans
  the whole live stack, from the most recent end to the least recent. There
  is no watermark (for now).
- The scan doesn't distinguish the frames. It treats every word from the
  top down to the base as an oop. That works because there are no dead
  words below the top (each frame starts at its caller's `stack_ptr + 1`,
  and its header, locals and arguments are all written when it's made), and
  because every word is either a real root, a tagged value, nullptr, or an
  address outside the collected range `[low, high)`, which `mark_object` and
  `update_pointer` already ignore:
  - the vtable and the method are in the image, above the heap;
  - the bytecode pointer is above 0x8000 on fructus, as above;
  - `locals`, `stack_ptr` and a `previousFrame` that is a stack frame point
    into the stack, below `low`;
  - on the host, the GC field and `bytecodeIndex` are small integers, far
    from the mmapped heap;
  - `context` (always a heap frame, since only frame functions make
    blocks) and a `previousFrame` that is a heap frame are real roots, and
    are marked and updated as they should be.
- The scan has to skip nullptr. It comes up for a method's `context`, the
  bootstrap frame's `previousFrame`, and on the host a `bytecodeIndex` or GC
  field of 0. `mark_object` and `update_pointer` assert against it today.
- The stack never moves, so pointers into it stay valid across a slide.
- Stores into stack frames need no write barrier and no remembering, since
  the stack is scanned at every collection. No store through an outer
  context can reach a stack frame, since stack functions make no blocks.
  Heap frames keep today's scheme: they're remembered when they stop being
  the current frame, and before a collection.

### As implemented

- The stack is only in the sliding heap (`USE_FRAME_STACK` in `defs.h`).
  The host's other collectors keep every frame in the heap.
- On the host, a block with a non-local return keeps its frames in the
  heap. When it has escaped, `Interpreter::sendEscapedBlock()` makes the
  block's frame not current and sends #escapedBlock: from the frame below
  it, so the new frame, and the pushes of the receiver and argument, would
  go where the block's frame is, and the send must leave that frame as it
  was if it fails. Fructus never sends #escapedBlock:, so the image writer
  puts those blocks on the stack too.
- The method has the size of its stack frame, 0 for a heap frame: in
  bytes on the host, set in `MakeStackRelative()`, so that any method made
  another way has heap frames, and in words in a byte after
  `numberOfArguments` on fructus, written by the image writer.
- The heap owns the stack (`SlidingHeap::GrowStack()`), and the interpreter
  keeps copies of its end (and base on the host) with `stackTop`, for the
  frame switches. On the host the stack has a reservation of its own, made
  accessible a page at a time, and on fructus it grows a pagelet at a time.
- Bumping fails like an allocation, so the send is retried after a
  collection, and gc-stress-retry mode makes it fail too.
- Frames on fructus have no `totalObjectSize`: `GetObjectSize()` computes it
  from the method, which is in the image and never moves. The host's frames
  keep it, since a host collector may move or free a method in the same
  collection as a frame of it, and still need the frame's size. Frames keep
  `locals` for now.

## Related

- The frame header still has `method` and `bytecodeIndex`. Storing the pc
  instead of the index would remove most uses of `method` (pc reloads, stack
  traces); returns could get the argument count from `locals` minus the
  start of the arguments. `Block>>restart` is the main other user.
- Size-segregated free lists for the common object sizes would help the
  remaining heap allocations, until the GC is moving and allocation is a
  bump.
