# The hashcode conundrum

**Not done, and only to be done if we have to.** For now, on fructus,
`Object>>hashcode` of an object in the heap is a doesNotUnderstand error
(see objHashcode() in src/primitives/Object.cpp). The image's objects,
which never move, hash by their address, and strings and integers by
their contents.

See gc.md for the shape of the GC.  Important points:

* Allocation grows down in a contiguous area
* Young space is at a lower address than old space
* Objects only slide, never change order
* Objects don't all have a has code field/method
* You can always take an identity hash code

## The problem

Most objects don't need an identity hash code, but we don't know which
when creating them.  We don't want to reserve space for the hash code
if it's almost always unused.

In a non-moving GC we could use the address of the object, but we want
a moving GC for fast allocation and no fragmentation.

## The solution

We steal two bits from the object.

* 00 A hash code was never taken
* 01 Has a hash code method/field
* 10 The hash code is the address
* 11 The hash code is in the table

## The hash code table

At the bottom of memory is the hash code table, growing up towards the
heap, which grows down.

It is sorted by object address and just consists of address-hashcode pairs.
We can do hash code lookups for 11-class objects with binary chop.

Every time an object moves from 00 to 10 (when a hash code is taken for the
first time) we reserve space between the table and the newest object by
incrementing a boundary beyond which no allocation can happen, but we don't
update the table.  We also need to track the counts of this for both old and
new generation. This ensures that a GC happens before we run out of space for
the table to grow.  Objects that use hash codes pay two words in memory.

## Updating the hash code table on a moving GC

While moving objects we check for 10-class objects that are moving and
count those that are not moving. As we move objects we add the
moved objects into the table with their old address (the hash code) and their
new location.  Non-moved 10-class objects are merely used to adjust the
accounting.  (Note we need to scan non-moved members of a generation anyway
to update their outgoing pointers.)

At the end of the GC we have some unsorted entries at the end of the table and
we can find the last unmoved object in the sorted section since objects are
sorted by descending location and we only moved the low address objects (either
because it was a minor GC or because none of the old objects died and got
moved).  We first squeeze the entries for dead objects out of the table, then
we can sort the newly added objects, but the effort is related O(nlogn) to the
percentage of the heap that was GCed, so the generational hypothesis is taken
advantage of.

## Characteristics

* Lookup in log(n) of the number of objects with hash codes (binary chop).
* Minor GC of n bytes of the heap where each object has a hash code with probability p makes O(pn log n) work updating the hash codes.
* Objects with no hash code don't pay in space or time.  Those with code pay 2 words, even if they don't move or use the reserved table space.
* Objects with hash code that don't move before they die don't need to update any central data structure

## Implementation

* Since a good chunk of the entries are already sorted, should probably use a sort that takes advantage of that eg. quicksort with middle element as pivot and that maintains a flag for each half that detects already sorted, no swaps so that part needs no more work.
* 2 bits needed in the object.

## Alternatives

* Monty: Add an indirection of the map pointer via an inserted word for hash codes.
  - Also probably two words per hash code
  - If you are willing to allocate during GC could save the two words for non-moved objects
  - Performance punishment for objects that don't use it because map is harder to find.
* JVM: Just spend the space in every object
* DartVM: You have to make a hashcode field and method to use an object as a hash key
* ??: Weak hash table, updated when hash codes are taken, when objects move and when objects die.
  - Could have a table per generation
  - Only have one for old gen, use the address in young gen, update the table when it survives (always promote).
  - Big table would be bottleneck for multithreaded languages.  Could make one table per GC page (128k).
* Grow the object when it moves - not compatible with sliding GC, can cause OOM on GC.
* Everyone else: Don't have a moving GC
