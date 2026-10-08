#pragma once

/*
 * The class library for fructus, compiled on the host and linked into the
 * fructus program as assembler.
 *
 * There is no SOM compiler on fructus. Instead the host VM compiles the
 * classes and writes them out with `-asm <dir>`: one .s file per class, in
 * the object layout fructus-elf-g++ uses, which fructus-elf-as assembles and
 * fructus-elf-ld links like any other part of the program. References
 * between objects are ordinary address constants, so the linker resolves
 * them, and there is no loader:
 *
 *  - A class is the global symbol som_class_<Name>, its metaclass
 *    som_meta_<Name>. A superclass in another file is an undefined symbol,
 *    so a missing class is a link error.
 *  - A selector, or the name of a global, is a number, the absolute ELF
 *    symbol som_sym_<mangled name>, which _selectors.s gives each, see
 *    vmobjects/VMSymbol.h, and not an object. Classes have no
 *    names, nor a field for one, and no names of fields, just how many.
 *    Since the class side is compiled on the host, where a class's first
 *    field is its name, the image writer shifts the class side's field
 *    indices down by one.
 *  - Each class adds a global to the som_globals section.
 *  - A method of a class is the global symbol <class label>.<mangled
 *    selector>, for the super sends in other files. A method has no
 *    constants: its bytecodes refer to the objects they push, see
 *    interpreter/bytecodes.h. The sends carry their inline caches, see
 *    interpreter/SendSite.h: they are unlinked, but a super send is linked
 *    already, since it doesn't depend on the receiver, so an invokable has
 *    no holder to find the superclass with, and a super send that isn't
 *    understood is an error of the image writer.
 *  - A primitive is an object in the image like a method, which refers to
 *    its routine by name, see primitives/FructusPrimitives.h. Nothing is
 *    installed at run time.
 *  - A #symbol in the program is an object of its own, which has nothing to
 *    do with the selector of the same name: the global som_symbol_<mangled
 *    name>, an instance of Symbol, which on fructus is an Object with a
 *    String of its characters, not a String itself. It's unique, so the
 *    program can compare symbols with == and use them as keys of identity
 *    dictionaries, and print them, but it can't send them or make new ones.
 *  - _universe.s holds nil, true, false, the system object and their
 *    globals, the program's main class, its arguments, the block classes,
 *    the selectors the VM uses, and the #symbols.
 *  - Each file has DWARF debug info, which isn't loaded: a subprogram for
 *    each method and block, and the source line of each bytecode.
 *
 * So fructus prints numbers where it would print names: a stack trace is
 * the pc of each frame, a selector is sel@<number> and a class is
 * class@<address>. tools/fructus-trace translates them with the ELF file:
 * the pcs into methods and source lines with the debug info, and the
 * selectors and classes with the symbol table.
 *
 * The world is closed: the program's selectors and methods are only for
 * dispatch and field names. No bytecode pushes them, and no object the
 * program can get at has them in a field, see Smalltalk-fructus. So there is
 * no perform, a message that isn't understood or an unknown global is an
 * error of the VM, which never sends doesNotUnderstand:arguments: (the
 * image writer warns about a class that has it). Objects hash by their
 * address, which is only right for the image's: the collector moves the
 * heap's, see ideas/hashcode.md.
 *
 * The image's objects that the program can store into are listed in the
 * som_roots section, which the collector walks, see WalkRoots().
 */

#include <cstddef>

#ifndef FRUCTUS
  #include <string>
  #include <vector>
#endif

/**
 * Where fructus-elf-g++ puts the fields of the VM objects, for writing them
 * on the host. FructusImage::CheckLayout() asserts these in the fructus
 * build, where they are checked against the real classes.
 */
namespace FructusLayout {
constexpr size_t word = 2;

/** A vtable pointer points past the offset-to-top and RTTI words. */
constexpr size_t vtableOffset = 2 * word;

// There is no GC field, and objects hash by their address, so there is no
// hash field: the fields follow the vtable.
constexpr size_t VMObject_totalObjectSize = 2;
constexpr size_t VMObject_numberOfFields = 4;
constexpr size_t VMObject_clazz = 6;
constexpr size_t VMObject_size = 8;

// no name: classes have none
/** The number of fields, tagged: the image has no field names. */
constexpr size_t VMClass_numberOfInstanceFields = 8;
constexpr size_t VMClass_instanceInvokables = 10;
constexpr size_t VMClass_superClass = 12;
constexpr size_t VMClass_size = 14;

constexpr size_t VMArray_size = 8;

/**
 * An integer that isn't a small one, with 64 bits that wrap around, see
 * som_box_int_t.
 */
constexpr size_t VMInteger_value = 2;
constexpr size_t VMInteger_valueSize = 8;
constexpr size_t VMInteger_size = 10;

constexpr size_t VMString_length = 2;
constexpr size_t VMString_chars = 4;
constexpr size_t VMString_size = 6;

/**
 * A selector, or the name of a global, is a number, not an object, see
 * VMSymbol.h: 2, 4, 6 and so on, below this, where no object is. Each is
 * an absolute ELF symbol, som_sym_<mangled name>, in _selectors.s.
 */
constexpr size_t selectorLimit = 0x1000;

constexpr size_t VMInvokable_signature = 2;
// no holder: the image writer links the super sends, see Interpreter.cpp

constexpr size_t VMMethod_bcLength = 4;
constexpr size_t VMMethod_numberOfLocals = 6;
constexpr size_t VMMethod_maximumNumberOfStackElements = 7;
constexpr size_t VMMethod_numberOfArguments = 8;
/** The bytecodes come right after the header, unaligned. */
constexpr size_t VMMethod_size = 9;

constexpr size_t VMLiteralReturn_literal = 4;
constexpr size_t VMLiteralReturn_numberOfArguments = 6;
constexpr size_t VMLiteralReturn_size = 8;

constexpr size_t VMGlobalReturn_globalName = 4;
constexpr size_t VMGlobalReturn_numberOfArguments = 6;
constexpr size_t VMGlobalReturn_size = 8;

constexpr size_t VMGetter_fieldIndex = 4;
constexpr size_t VMGetter_numberOfArguments = 6;
constexpr size_t VMGetter_size = 8;

constexpr size_t VMSetter_fieldIndex = 4;
constexpr size_t VMSetter_argIndex = 6;
constexpr size_t VMSetter_numberOfArguments = 8;
constexpr size_t VMSetter_size = 10;

/**
 * A send bytecode and its inline cache, see interpreter/SendSite.h. A unary
 * send has no number of arguments, so its cache is a byte earlier.
 */
constexpr size_t SendSite_numberOfArguments = 1;
constexpr size_t SendSite_target = 2;
constexpr size_t SendSite_receiverClass = 4;
constexpr size_t SendSite_length = 6;
constexpr size_t SendSite_unaryTarget = 1;
constexpr size_t SendSite_unaryLength = 5;

/**
 * VMPrimitive and VMSafe*Primitive: a Prim, which is just a routine. A
 * VMPrimitive has the number of arguments too, which a VMSafe*Primitive's
 * kind gives.
 */
constexpr size_t VMPrimitive_prim = 4;
constexpr size_t VMPrimitive_numberOfArguments = 6;
constexpr size_t VMPrimitive_size = 8;
constexpr size_t VMSafePrimitive_size = 6;

constexpr size_t VMEvaluationPrimitive_numberOfArguments = 4;
constexpr size_t VMEvaluationPrimitive_size = 6;

/**
 * Small integers are tagged, so they have 15 bits. Other integers are a
 * VMInteger.
 */
constexpr long smallIntMin = -(1L << 14);
constexpr long smallIntMax = (1L << 14) - 1;

constexpr size_t PaddedSize(size_t n) {
    return (n + word - 1) & ~(word - 1);
}
}  // namespace FructusLayout

#ifdef FRUCTUS
  #include "../vmobjects/ObjectFormats.h"
#endif

class VMArray;
class VMClass;
class VMObject;
class VMSymbol;

/** The block classes are Block1 to Block<maxBlockClassArguments>. */
constexpr int maxBlockClassArguments = 4;

/**
 * The selectors the VM uses, which the image always has a number for, as
 * P(mangled, selector).
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FRUCTUS_VM_SYMBOLS(P)            \
    P(new, "new")                         \
    P(run, "run")                         \
    P(run_3a, "run:")                     \
    P(bootstrap, "bootstrap")

#ifdef FRUCTUS
  #define FRUCTUS_DECLARE_SYMBOL(mangled, selector) \
      extern "C" VMSymbol som_sym_##mangled;
FRUCTUS_VM_SYMBOLS(FRUCTUS_DECLARE_SYMBOL)
  #undef FRUCTUS_DECLARE_SYMBOL
#endif

/** Friend of the VM object classes, to read and check their fields. */
class FructusImage {
public:
#ifdef FRUCTUS
    /** Set the well-known objects and classes. */
    static void Load();

    static VMObject* SystemObject();

    /** The class the program was written with, and its arguments. */
    static VMClass* MainClass();
    static VMArray* Arguments();

    /** #run: if the main class has it, otherwise #run. */
    static VMSymbol* MainSelector();

    /** The class of blocks with `numberOfArguments`, or nullptr. */
    static VMClass* BlockClass(uint8_t numberOfArguments);

    /** The value of a global, or nullptr. */
    static vm_oop_t GetGlobal(VMSymbol* name);
    /**
     * Walks the objects of the image that can point into the heap, for the
     * collector. With `onlyRemembered`, only those whose card the write
     * barrier dirtied, which are the only ones that can point to young
     * objects.
     */
    static void WalkRoots(walk_heap_fn walk, bool onlyRemembered);

    static void CheckLayout();
#else
    /**
     * Write the classes the program needs, and its arguments, to `dir`.
     * The first argument is the main class.
     */
    static void Write(const std::string& dir,
                      const std::vector<std::string>& arguments);

private:
    // in compiler/FructusImageWriter.cpp
    class AsmFile;
    class ImageWriter;
#endif
};
