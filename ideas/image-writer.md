# An image writer: compile the class library on the host

## Why

There is no SOM compiler on fructus, and there shouldn't be: the parser,
lexer, and compiler are ~31 KB of x86-64 code, and the core library is 59 KB of
source. So the host compiles the classes, and the result is linked into the
fructus program. Today `LoadImage()` in `src/vm/Universe.cpp` is a stub that
only installs the primitives.

There is no existing bytecode or image format to reuse (see below), so this is
new, but the in-memory form is already close to what we need.

## What the compiler produces today

- `VMMethod`: a byte array of bytecodes, a constants array
  (`indexableFields`), `numberOfArguments`, `numberOfLocals`,
  `maximumNumberOfStackElements`, `signature`, `holder`.
- The bytecode set is `src/interpreter/bytecodes.h`, lengths in
  `Bytecode::bytecodeLengths`. All operands are small integers: constant,
  field, local, and argument indexes, context levels, and 1- or 2-byte jump
  offsets. **Bytecode arrays contain no pointers**, and nothing modifies them
  at run time, so they can be copied verbatim.
- Control flow (`ifTrue:`, `whileTrue:`, `to:do:`, ...) is already inlined
  into jumps by the compiler.
- References go through the constants array: symbols (selectors, globals),
  the methods of nested blocks, and literals (small integers, strings,
  arrays; on the host also big integers and doubles).
- `VMClass`: name, superclass, instance field names (an array of symbols),
  instance invokables (an array), and a metaclass, which is another
  `VMClass`.
- Trivial methods are not bytecode: `VMGetter`, `VMSetter`,
  `VMLiteralReturn`, `VMGlobalReturn` hold an index or a literal.
- Methods declared `= primitive` are bound by `PrimitiveLoader`, by class
  name and selector, when the class is loaded.
- `src/compiler/Disassembler.cpp` (`-d`) is the closest thing to a format:
  human-readable text with no reader. It shows how to walk classes, methods,
  and constants.

## The idea

Add an option to the host VM, e.g.

```
SOM++ -cp Smalltalk:... --write-image fructus-image.cpp MainClass
```

It boots as usual, loads `MainClass` and everything it needs, then walks the
object graph from the globals (and the symbol table) and writes it out in
**fructus layout**: 16-bit words, 15-bit tagged small integers, and the field
order and sizes that `fructus-elf-g++` uses for each class.

### Output: generated C++, compiled for fructus

Generated C++ (or assembler) source, compiled by `fructus-elf-g++` and linked
into `SOM++`, lets the linker do the work:

- Each object is a static variable with its own name, so references between
  objects are ordinary address constants and the linker resolves them; no
  relocation at load time.
- The vtable pointer of each object can be an address constant, too: the
  vtable symbol plus two words, e.g. `_ZTV8VMMethod + 4`. Then no loader
  fixup is needed. (The alternative is a type tag per object and a loader
  that stores the vtable pointers it captures in
  `obtain_vtables_of_known_classes()`.)
- The objects can be emitted as plain structs that mirror the layout of the
  real classes. The generated file can `static_assert` that each mirror's
  `offsetof()` and `sizeof()` match the real class as fructus compiles it, so
  a field change in the VM can't silently break the image.
- Variable-sized objects (methods with their constants and bytecodes,
  strings and symbols with their characters, arrays) are a struct for the
  fixed part followed by the inline data.

### What the writer must convert or reject

- Small integers outside 15 bits, big integers, and doubles: an error, since
  fructus has none.
- Object hashes (`VMObject::hash`, `VMInvokable::hash`) are addresses on the
  host; recompute them, or drop the field (see
  [field-order-for-short-offsets.md](field-order-for-short-offsets.md)).
- Interior pointers (`VMMethod::indexableFields`, `VMMethod::bytecodes`,
  `VMString::chars`) must point into the fructus object, unless they are
  removed first.
- Symbol inline caches (`cachedClass_invokable`, `cachedInvokable`,
  `nextCachePos`) start empty.
- Decisions that belong on the host: the default `Vector` capacity (50, which
  is a lot of a 16 KB heap), and whether `Vector` methods are replaced by
  primitives (the bytecode hash check could run here instead of on fructus).

### Loading on fructus

`LoadImage()` then has to:

- Register the image's symbols in the symbol table, or the writer emits the
  table itself.
- Set the globals, or the writer emits the globals vector.
- Bind primitives. Simplest first: keep `LoadPrimitives()` at startup, as now.
  Later the writer could emit the primitive objects directly, but the
  primitive functions are `static` in their `.cpp` files, so they would need
  to become visible to the generated file, or be referenced through a
  generated table of numbered primitives. That would also drop
  `PrimitiveContainer`/`PrimitiveLoader` (~2 KB) and the selector strings.

### The GC

Image objects are outside the mark-sweep heap, which matters:

- **Marking writes `gcField`**, so image objects must be in RAM (`.data`),
  not ROM.
- **`sweep()` only clears marks inside the heap**, so image objects would
  stay marked after the first GC and never be traced again, and heap objects
  they point to (e.g. a class's invokables array replaced by
  `InstallPrimitive`, a global set at run time, a symbol's inline cache)
  would be freed. Options: clear the image objects' marks after each GC,
  flip the meaning of the mark bit on each GC, or treat the image as roots
  and never mark image objects at all (mark only objects inside
  `[heap, heapEnd)`, but trace the image's pointer fields every GC).

### ROM later

Bytecodes never change, so they could be in ROM (`ld/fructus-rom16k.ld`) if
`VMMethod::bytecodes` keeps being a pointer instead of being computed. The
object headers can't, because of `gcField`, and neither can symbols, because
of their inline caches.

## Steps

1. Write the layout mirrors and `static_assert`s for the classes that occur in
   the image: `VMObject`, `VMClass`, `VMArray`, `VMString`, `VMSymbol`,
   `VMMethod`, the trivial methods, and the primitives if they are emitted.
   (`VMBlock` objects are only created at run time; a nested block is a
   `VMMethod` in its outer method's constants.)
2. Host side: walk the graph from the globals and symbols, assign each
   object a name, and print the C++.
3. Fructus side: `LoadImage()` registers symbols and globals, and installs
   primitives.
4. Fix the GC for image objects.
5. Run `Hello.som` in the simulator, then measure the image: bytes of
   `.data` per class and method, to see how much of the 64 KB the core
   library needs, and which classes are worth trimming.
