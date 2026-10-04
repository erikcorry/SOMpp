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
 *  - A symbol is som_sym_<mangled name>, defined in a COMDAT group of the
 *    same name by every file that uses it, so the linker keeps one copy and
 *    symbols stay unique. There is no symbol table, and no symbols are made
 *    at run time.
 *  - Each class adds a global to the som_globals section.
 *  - A method of a class is the global symbol <class label>.<mangled
 *    selector>, for the super sends in other files. A method has no
 *    constants: its bytecodes refer to the objects they push, see
 *    interpreter/bytecodes.h. The sends carry their inline caches, see
 *    interpreter/SendSite.h: they are unlinked, but a super send is linked
 *    already, since it doesn't depend on the receiver.
 *  - A primitive is an object in the image like a method, which refers to
 *    its routine by name, see primitives/FructusPrimitives.h. Nothing is
 *    installed at run time.
 *  - _universe.s holds nil, true, false, the system object and their
 *    globals, the program's main class, its arguments, the block classes,
 *    and the symbols the VM uses.
 *
 * The world is closed: the program's symbols and methods are only for
 * dispatch and field names. No bytecode pushes them, and no object the
 * program can get at has them in a field, see Smalltalk-fructus. So there is
 * no perform, a message that isn't understood or an unknown global is an
 * error of the VM, and since nothing moves, objects hash by their address.
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

constexpr size_t gcField = 2;

// objects hash by their address, so there is no hash field
constexpr size_t VMObject_totalObjectSize = 4;
constexpr size_t VMObject_numberOfFields = 6;
constexpr size_t VMObject_clazz = 8;
constexpr size_t VMObject_size = 10;

constexpr size_t VMClass_name = 10;
constexpr size_t VMClass_instanceFields = 12;
constexpr size_t VMClass_instanceInvokables = 14;
constexpr size_t VMClass_superClass = 16;
constexpr size_t VMClass_size = 18;

constexpr size_t VMArray_size = 10;

constexpr size_t VMString_length = 4;
constexpr size_t VMString_chars = 6;
constexpr size_t VMString_size = 8;

/** A symbol isn't an object: no vtable or GC field, see VMSymbol.h. */
constexpr size_t VMSymbol_numberOfArgumentsOfSignature = 0;
constexpr size_t VMSymbol_length = 1;
constexpr size_t VMSymbol_size = 2;
constexpr size_t VMSymbol_maxLength = 255;

constexpr size_t VMInvokable_signature = 4;
constexpr size_t VMInvokable_holder = 6;

constexpr size_t VMMethod_bcLength = 8;
constexpr size_t VMMethod_numberOfLocals = 10;
constexpr size_t VMMethod_maximumNumberOfStackElements = 11;
constexpr size_t VMMethod_numberOfArguments = 12;
/** The bytecodes come right after the header, unaligned. */
constexpr size_t VMMethod_size = 13;

constexpr size_t VMLiteralReturn_literal = 8;
constexpr size_t VMLiteralReturn_numberOfArguments = 10;
constexpr size_t VMLiteralReturn_size = 12;

constexpr size_t VMGlobalReturn_globalName = 8;
constexpr size_t VMGlobalReturn_numberOfArguments = 10;
constexpr size_t VMGlobalReturn_size = 12;

constexpr size_t VMGetter_fieldIndex = 8;
constexpr size_t VMGetter_numberOfArguments = 10;
constexpr size_t VMGetter_size = 12;

constexpr size_t VMSetter_fieldIndex = 8;
constexpr size_t VMSetter_argIndex = 10;
constexpr size_t VMSetter_numberOfArguments = 12;
constexpr size_t VMSetter_size = 14;

/** A send bytecode and its inline cache, see interpreter/SendSite.h. */
constexpr size_t SendSite_numberOfArguments = 1;
constexpr size_t SendSite_target = 2;
constexpr size_t SendSite_receiverClass = 4;
constexpr size_t SendSite_length = 6;

/** VMPrimitive and VMSafe*Primitive: a Prim, which is just a routine. */
constexpr size_t VMPrimitive_prim = 8;
constexpr size_t VMPrimitive_size = 10;

constexpr size_t VMEvaluationPrimitive_numberOfArguments = 8;
constexpr size_t VMEvaluationPrimitive_size = 10;

/** Small integers are tagged, so they have 15 bits. */
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
 * The symbols the VM uses, which the image always has, as
 * P(mangled, selector).
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FRUCTUS_VM_SYMBOLS(P)            \
    P(new, "new")                         \
    P(run, "run")                         \
    P(run_3a, "run:")                     \
    P(escapedBlock_3a, "escapedBlock:") \
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
    static void WalkGlobals(walk_heap_fn walk);

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
