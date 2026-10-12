# Fix the instance field count upstream

## Background

On upstream SOM++ (`master` here, and the original 2007 code before it), a
class's `instanceFields` array holds the names of **all** its fields,
including those of its superclasses: the parser copies the superclass's
array in first, with `ClassGenerationContext::SetInstanceFieldsOfSuper()`.

`VMClass::GetNumberOfInstanceFields()` then adds the superclass's count on
top:

```cpp
size_t VMClass::GetNumberOfInstanceFields() const {
    return load_ptr(instanceFields)->GetNumberOfIndexableFields() +
           numberOfSuperInstanceFields();   // already in the array
}
```

so the count of a class is the sum of the counts of all its superclasses.
For a class with `n` levels of superclasses that have fields, the fields are
counted up to `n + 1` times.

## What it costs

`Universe::NewInstance()` and `Universe::NewClass()` size objects with this
count, so:

- **Instances of subclasses are too big.** `B = A ( | b c | )` with
  `A = ( | a | )` has 3 fields, and gets 3 + 1 = 4, one of them always nil.
  Deeper hierarchies waste more: every level adds the whole count of the one
  above. On the host that is 8 bytes a slot in every instance, and more for
  the GC to scan.
- **Classes are much too big.** A metaclass's field names start with the
  five of Class (`class name instanceFields instanceInvokables superClass`),
  so `Planner class` counts 5 + 5 (for `Object class`) = 10, and
  `NewClass()` then adds `VMClassNumberOfFields` (4) on top: 14 fields where
  4 or 5 are used.
- **`instVarNamed:` answers the wrong field.** `GetInstanceFieldName()`
  subtracts the superclass's count from the index before looking in the
  array, which already starts with the superclass's names. With the classes
  above, `(B new) instVarNamed: #b` answers the value of `c`, and
  `instVarNamed: #c` answers nil. `LookupFieldIndex()` also loops up to
  `<= count`, which reads past the array for `Object`.

## The fix

As done on the fructus branch (where it saved 718 bytes of DeltaBlue's
image and 254 of Hello's, and makes the GC scan less):

1. `GetNumberOfInstanceFields()` returns the length of the array, and
   `numberOfSuperInstanceFields()` goes.
2. `GetInstanceFieldName(index)` indexes the array directly.
3. `LookupFieldIndex()` loops `i < count`, and the comment about the class
   field goes.
4. `Universe::NewClass()` makes the class with the metaclass's field count
   **minus** `VMClassNumberOfFields`, since the `VMClass` constructor adds
   those itself, and with no extra fields if that isn't positive.
   `VMClassNumberOfFields` becomes public for this.

Upstream, check as well:

- The unit tests (`src/unitTests`), which may assert the old sizes, e.g.
  `CloneObjectsTest` and `WalkObjectsTest`.
- Anything else that sizes objects from the class, e.g. a snapshot or image
  format, and the OMR variant.
- A test for `instVarNamed:` and `instVarAt:` on a subclass, which would have
  caught it: the TestSuite has none.

## A second bug: class-side fields are off by one

The five names of Class start with `class`, which is the `clazz` in the
object header, not a field. But class-side code indexes the class object's
fields by position in that list, so every name is one field too far:

```smalltalk
ClassFieldTest = (
    ----
    fieldName = ( ^ name )              "answers the instanceFields Array"
    fieldSuperClass = ( ^ superClass )  "answers nil"
)
```

`name` reads `instanceFields`, `superClass` reads the slot after the
`VMClass` fields, and class variables start one slot later than they could,
leaving one slot unused in every class object (the 5th field, after the
fix above).

The fix, also done on the fructus branch, takes `class` out of the
hardcoded list in `Parser::superclass()`, which shifts every class
variable's index down by one. Then a class object has exactly
`VMClassNumberOfFields` plus its class variables, and `name`, `superClass`
and so on answer the right fields. That saved another 132 bytes of
DeltaBlue's image. Nothing in the class library reads Class's fields
directly, which would be why it hasn't shown up, but the reflection
primitives (`instVarAt:` on a class) answer differently now, so a test for
those belongs with it upstream.
