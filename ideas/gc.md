# GC plan for SOM++ on Fructus

## Decisions (2026-10-08)

* No fixed old-young boundary: the young space is everything below the
  lowest old object, wherever that is.  The write barrier ideas that assume a
  fixed boundary (bit-fiddling, "only filter on the stored value") are ruled
  out.  The barrier marks the holder's byte for every store that isn't a
  small integer or an immortal, or for every store.
* Instant promotion: every collection promotes all survivors, so afterwards
  the young space is empty and the remembered set is clear.  There is no
  remembered set upkeep during a collection.
* The remembered set covers the whole 64k, 512 bytes, so the barrier needs no
  bounds check.  The top half covers the image above the heap.  The image
  writer puts the image objects that can hold heap pointers (class objects and
  so on) in a section the linker gives start and stop symbols for, and a
  minor collection walks that list as roots, using the bytes of the top half
  as a filter: it scans only the objects whose byte is marked.
* Sliding doesn't use object_starts: it scans the mark bits down for a run of
  1s.  The bottom of a run is an object start, since the whole extent of
  objects is marked.  The run moves up as a block (overlapping, so a memmove,
  or a descending ldm/stm copy, postponed), then its objects are scanned
  forwards to update their pointers and object_starts: the first object in
  each pagelet in a run writes the pagelet's entry, and a lower run may
  overwrite what a higher run wrote.  Then the next run down.
* Interior pointers (a frame's locals and stack pointer) move with their
  object, by the run's distance.
* Unreachable old objects are walked (the remembered set scan can't know which
  old objects are reachable, and the gray pagelet rescan steps over unmarked
  objects by their size), but no object known to be dead is.
* Hash codes on fructus are a virtual method that not all objects have, like
  Dart.  Image objects don't move, so they can hash by address.
* Fructus has popcount (and bitrev and clz).
* The host build has the same GC as a GC_TYPE, for testing, with the same
  128-byte pagelets (object_starts stores one byte), and the granule, mark
  word and heap size scaled.  The host heap is page aligned, reserved with
  mmap and committed with mprotect.

* No allocation during GC - no unexpected OOM
* Don't waste memory on semispaces
* No fragmentation
* Generational
* Simple write barrier
* Bump allocator
* No slow pointer reversal

Compromises

* Moderate pauses on major GC
* All objects need a header

Loosely based on Palcom, Dartino, Toit GCs.  Related to DartVM GC.
Related to some research GC the name of which I've forgotten.

Based on pagelets of 128 bytes and a max heap extent of the 0-32k range.
Heap area assumed contiguous (can work with non-contiguous, but much
higher complexity).

## object_starts

We maintain an object_starts byte array, one entry per 128-byte pagelet (256
bytes for the 32k extent).
It's at 0x300, which is in the ZP.  When the interpreter is
written in assembler we expect to have a register pointing at 0x200 to
access all the zp objects with imm10 addressing.  The point of object_starts
is that we can always iterate a small part of the heap by starting at a known
object boundary.

On every allocation we write the low byte of the address of the new object to
the object_starts array.  Since objects are tagged with 0 this will always be
an even number, so we initialize with 1 or -1 to indicate no known objects
start on the pagelet. Since allocation and compaction goes from high to low
address, the address in the object_starts array is always the first object on the
pagelet!  Since a pagelet is less than 256 bytes we only need the low byte 
of the address to determine the address of the first object.

Allocations are expected to be both in the interpreter and in C++ code.

## Allocation

A bump allocator goes down. At any time it points at the latest object
allocated.  Normally allocation will start at 32k.

(Need to fix SOM++ to do a GC on any allocation!)

## Write barrier and remembered_set

We remember which old space objects might contain pointers to young space
so we can minimize the scanning for minor GCs.

A byte per pagelet determines if the pagelet contains objects in the remembered
set.  Normally we expect 256 bytes to be enough. A 0 indicates being in the
set, which reduces register pressure since the base register is always 0 in its
low 8 bits.

## Updating the remembered set

Several ideas considered. The remembered set has backing even for young
space so we don't strictly need to avoid recording stores in young space.

I think I like the "Only filter on the stored value" variant below.

The one with the fudge factor is interesting and flexible wrt generation
sizing though.  Would mean the young gen could be large (fast) when the
old gen was almost empty.

### Detecting which stores need to update the remembered set, bit-fiddling idea

The old-young boundary is at 0x4000 (can objects straddle the
boundary?  Probably OK.  A straddling object is considered part of the young
space.)  A write of a young-gen (&lt; 0x4000) pointer to an object
that is in the old gen (object start >= 0x4000) forces a write
to the remembered set, probably located at 0x200-0x2ff.

Assuming we never write to program objects >= 0x8000 we need one free
reg for the remembered set, plus the register that just held the
pointed-to object.
´´´
  st r0, [r1, #field_offset]                  ; May need wb.
  isset r5, r0, 0xc000                        ; r5 = not_in_young_space.
  or r0, r5, r0                               ; r0 = safe (not in young space or SMI).
  lsr r5, r1, #14                             ; r5 is 1 if we are in old space, 0 for young space.
  lsr r5, r5, r0                              ;  = old_space & ~safe.
  br eq, r5, #1, .need_wb
  ;  Dispatch next byte code or ret.
.need_wb:
  lsr r5, r1, #7                              ; Get index in remembered set.
  st8 base [base, r5]                         ; Store 0 in rembered set.
  ;  Dispatch next byte code or ret.
´´´

### Detecting which stores need to update the remembered set, pointer-direction idea

Only pointers that go down need to update the remembered set and if we can say they
have to go a long way down before they need to update, which could mean the lowest
pagelet(s) of the old gen were always implicitly in the remembered set even if the
pagelet was not marked.

One temp register needed.  Zero if the stored value is dead.

´´´
  st r0, [r1, #field_offset]                  ; May need wb.
  shl r5, r0, #15                             ; Smi -> 0x8000, non-smi -> 0
  or r0, r0, r5                               ; Make Smis look like unproblematic high adresses
  sub r5, r1, #256                            ; Fudge factor - last 256 bytes of old gen are not tracked
  br lt, r0, r5, .wb_needed
  ;  Dispatch next byte code or ret.
.need_wb:
  lsr r5, r1, #7                              ; Get index in remembered set.
  st8 base [base, r5]                         ; Store 0 in rembered set.
  ;  Dispatch next byte code or ret.
´´´

Question is how many false alarms this makes.  The remembered set is always precise after
a GC.

Also works for variable old-young boundary, three generations.

### Just always write the byte

A lot of old-space objects don't get updated and for those that do they are often updated with
pointers to new space anyway, so why filter?  The card marking doesn't cost much, Urs-Hölzle
style.

No temps if the written pointer is dead.

´´´
  st r0, [r1, #field_offset]                  ; May need wb.
  lsr r5, r1, #7                              ; Get index in remembered set.
  st8 base [base, r5]                         ; Store 0 in rembered set.
  ;  Dispatch next byte code or ret.
´´´

Pollutes the remembered set on Smi writes, nil writes, bool writes.

Stores are slow but not really slower than all the cleverness above.  Costs a
bit more if we run out of registers and have to load the base register every
time:

´´´
  st r0, [r1, #field_offset]                  ; May need wb.
  lsr r5, r1, #7                              ; Get index in remembered set.
  add r5, r5, #0x200                          ; Immbit5 register, so two-byte insn.
  st8 r1 [r5, #0]                             ; Store even number in rembered set.
  ;  Dispatch next byte code or ret.
´´´

## Only filter on the stored value

Assume the old-new border is at 0x4000

´´´
  st r0, [r1, #field_offset]                  ; May need wb.
  shl r5, r0, #14                             ; Smi -> 0x8000, non-smi -> 0
  or r0, r0, r5                               ; Make Smis look like unproblematic high adresses
  brclr r0, 0xc000, need_wb                   ; Jump if high bits zero (young gen) and was heap tagged (0)
  ;  Dispatch next byte code or ret.
.need_wb:
  lsr r5, r1, #7                              ; Get index in remembered set.
  st8 base [base, r5]                         ; Store 0 in rembered set.
  ;  Dispatch next byte code or ret.
´´´

Equivalent:

´´´
  st r0, [r1, #field_offset]                  ; May need wb.
  isset r5, r0, #0xc000                       ; old gen -> 1, young gen -> 0
  or r0, r0, r5                               ; Smis are like old gen (1-tagged)
  brclr r0, #1, need_wb                   ; Jump if high bits zero (young gen) and was heap tagged (0)
  ;  Dispatch next byte code or ret.
.need_wb:
  lsr r5, r1, #7                              ; Get index in remembered set.
  st8 base [base, r5]                         ; Store 0 in rembered set.
  ;  Dispatch next byte code or ret.
´´´

More branches, fewer registers - no temps if stored value is dead:

´´´
  st r0, [r1, #field_offset]                  ; May need wb.
  brclr r0, #1, .not_smi                      ; 4 cycles when taken :-(
  ; Dispatch or ret.
.not_smi
  brset r0, #0xc000, .not_young_gen           ; 4 cycles when taken :-(
  ; Dispatch or ret.
.not_young_gen
  lsr r0, r1, #7                              ; Get index in remembered set.
  st8 base [base, r0]                         ; Store 0 in rembered set.
  ;  Dispatch next byte code or ret.
´´´

Sadly brclr doesn't take a register or this constant:

´´´
  st r0, [r1, #field_offset]                  ; May need wb.
  brclr r0, #0xc001, .need_wb                 ; Sadly impossible.
  ;  Dispatch next byte code or ret.
.need_wb:
  lsr r5, r1, #7                              ; Get index in remembered set.
  st8 base [base, r5]                         ; Store 0 in rembered set.
  ;  Dispatch next byte code or ret.
´´´

Only filter on immortals/oddballs above 32k and smis, write all others (make no
assumptions on location of old-new boundary):

´´´
  st r0, [r1, #field_offset]                  ; May need wb.
  isset r5, r0, #0x8000                       ; immortals gen -> 1, young gen -> 0
  or r0, r0, r5                               ; Smis are like old gen (1-tagged)
  brclr r0, #1, need_wb                       ; Jump if high bits zero (mutable heap) and was heap tagged (0)
  ;  Dispatch next byte code or ret.
.need_wb:
  lsr r5, r1, #7                              ; Get index in remembered set.
  st8 base [base, r5]                         ; Store 0 in rembered set.
  ;  Dispatch next byte code or ret.
´´´

## Garbage collections

### Marking

We start by marking all live objects either in the whole heap or in just the young
space for minor collections.  The marking stack is constant size so we have to can
cope with mark stack overflow.  A contiguous bitmap indicates blackness and a tiny
per-pagelet gray marker (grayness_table, one bit per pagelet, 32 bytes in all)
helps track grayness in the presence of overflows:

* White:  Mark bit is zero
* Gray:   Mark bit is one and object is either on the mark stack or its pagelet is marked gray in the grayness_table
* Black:  Mark bit is one, not gray

Because the grayness_table is coarse we sometimes mark objects gray when they were
black. This causes a harmless rescan when the mark stack overflows.

Allocation granularity is probably 4 bytes, so the mark_bits array has one 16 bit
word covering 64 bytes.  For the 32k region we need 512 words or 1k of marking
bytes.  When we mark an object we mark its full extent, not just the bit corresponding
to the object address.

Rescanning the gray pagelets and scanning the remembered set is done in a similar
way. Most pagelets can be skipped based on the grayness bitmap and the remembered
set bytemap. For the pagelet we are scanning we unmark the pagelet first in its
bitmap or bytemap (it may get remarked).

We find the first object start for the pagelet based on the object_starts
array.

When iterating remembered set objects that start in the correct pagelet we
set the remembered set if any object still has a young-space pointer (it
may have been overwritten, which is why we cleared it before scanning the
pagelet).

When iterating gray pagelets we skip objects that don't have their mark bit
set.  We may get another marking stack overflow in which case we have to set a
bit again, and it may even be the bit for the current pagelet.

### Compaction

We never sweep or have freelists, only compact.

We are moving object up in memory only, never down.  To determine their new
location we will use popcount on the marking bits, plus an array that
accelerates the calculation.  An object at location _old_ of size _size_ is
relocated to a new location of:

0x8000 - size - 4 * popcount_above(old + size);

which is the same as 0x8000 - 4 * popcount_above(old).  Since the whole extent
of an object is marked, this also works for interior pointers.

The popcount_above function is the total number of 1-bits corresponding to all
the objects originally at or above the given location.  To speed up
popcount_above we precalculate the number of cumulative 1-bits in the marking
map for each 256 byte page (two pagelets).  Since four 16 bit mark words cover
a 256 byte page that means we only actually have to do 0 to 4 popcount operations
to get the new location.  The cumulative_mark_count array is calculated with a
single backwards sweep of the mark bits and takes 256 bytes (128 words).

Since the new location can be calculated using only the old location, the
marking bits, and the cumulative_mark_count array it is easy to update
pointers when we encounter them.

The mark array is used to find the survivors and where they are moved to (high
to low address), as they are moved, any pointers they contain are
updated.  We scan from high to low addresses moving objects.  Start at the
top of memory for major GCs, or at 0x4000 for minor GCs since we are
compacting upwards.

In a minor GC we use the remembered set again to find and update pointers to
surviving young objects.

While moving objects we update the object_starts.  Again because we are going
high to low we can effortlessly maintain the invariant that it records the
first object in the pagelet, but we need to clear it for pagelets with no
objects in them because the remembered set is probably a bit imprecise.

When moving on major GCs we also need to update remembered set as we go.
Annoying, and the alternative is to declare bankruptcy, mark everything
as being in the rs and rebuild on the next minor GC. That's probably worse.

## Summary

Every object needs a header so it knows its size and pointer/tagged fields.
This is what keeps the heap iterable.  The object_starts means we can iterate
random access pagelets.

The memory map as implemented (2026-10-08), see cmake/fructus-som.ld. The
tables are sized for the heap at startup, from what the program leaves.
Nothing above 32K is written apart from the inline caches of the methods,
since in one mode the top 32K is the frame buffer for writes.

* 0000-0002  `jmp _start`
* 0003-01ff  zp_lo (just theInterpreter now: no malloc)
* 0200-02ff  256 bytes of remembered set: 1 byte per pagelet of the low 32K,
             the heap and the image's writable objects
* 0300-03ff  zp_hi, unused
* 0400-04ff  IO page, whose guard traps a stack overflow
* 0500-06ff  native stack, 512 bytes (frames are on the SOM heap; 312 bytes
             at most in the tests and AWFY)
* 0700-      object_starts (1 byte per pagelet), mark bits (1 bit per 4
             bytes), cumulative mark counts (1 word per 256 bytes, also the
             marking stack), gray bits (1 bit per pagelet): about 4.9% of
             the heap
*      -rw   GCed contiguous heap, to below the writable part
* rw  -7fff  the image's objects the program stores into (classes, plain
             objects, arrays: the som_roots), and the C++ .data and .bss
* ro  -ffff  code, constants, the rest of the image; at 0x8000, or lower if
             it's bigger than 32K

About 2k is overhead used or 7% of the heap.  In return:

* No allocation during GC
* No allocation of forwarding tables
* No pointer reversal
* Fixed size remembered set updated with very few insns
* No problems with mark stack overflow
* No fragmentation
* Bump allocation
* Generations
