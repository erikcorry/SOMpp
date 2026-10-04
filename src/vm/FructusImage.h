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
 *    symbols stay unique. The group also holds the symbol's entry in the
 *    som_symbols section, a table of all symbols.
 *  - Each class adds a Universe::Global to the som_globals section.
 *  - A primitive is an object in the image like a method, which refers to
 *    its routine by name, see primitives/FructusPrimitives.h. Nothing is
 *    installed at run time.
 *  - _universe.s holds nil, true, false, and som_arguments, the arguments
 *    the program was written with.
 *
 * FructusImage::Load() (in ImageFructus.cpp) registers the tables the linker
 * gathered, between __start_<section> and __stop_<section>.
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

constexpr size_t VMObject_hash = 4;
constexpr size_t VMObject_totalObjectSize = 6;
constexpr size_t VMObject_numberOfFields = 8;
constexpr size_t VMObject_clazz = 10;
constexpr size_t VMObject_size = 12;

constexpr size_t VMClass_name = 12;
constexpr size_t VMClass_instanceFields = 14;
constexpr size_t VMClass_instanceInvokables = 16;
constexpr size_t VMClass_superClass = 18;
constexpr size_t VMClass_size = 20;

constexpr size_t VMArray_size = 12;

constexpr size_t VMString_length = 4;
constexpr size_t VMString_chars = 6;
constexpr size_t VMString_size = 8;

constexpr size_t VMSymbol_numberOfArgumentsOfSignature = 8;
constexpr size_t VMSymbol_cachedClass_invokable = 10;
constexpr size_t VMSymbol_nextCachePos = 16;
constexpr size_t VMSymbol_cachedInvokable = 18;
constexpr size_t VMSymbol_size = 24;

constexpr size_t VMInvokable_hash = 4;
constexpr size_t VMInvokable_signature = 6;
constexpr size_t VMInvokable_holder = 8;

constexpr size_t VMMethod_numberOfLocals = 10;
constexpr size_t VMMethod_maximumNumberOfStackElements = 12;
constexpr size_t VMMethod_bcLength = 14;
constexpr size_t VMMethod_numberOfArguments = 16;
constexpr size_t VMMethod_numberOfConstants = 18;
constexpr size_t VMMethod_indexableFields = 20;
constexpr size_t VMMethod_bytecodes = 22;
constexpr size_t VMMethod_size = 24;

constexpr size_t VMLiteralReturn_literal = 10;
constexpr size_t VMLiteralReturn_numberOfArguments = 12;
constexpr size_t VMLiteralReturn_size = 14;

constexpr size_t VMGlobalReturn_globalName = 10;
constexpr size_t VMGlobalReturn_numberOfArguments = 12;
constexpr size_t VMGlobalReturn_size = 14;

constexpr size_t VMGetter_fieldIndex = 10;
constexpr size_t VMGetter_numberOfArguments = 12;
constexpr size_t VMGetter_size = 14;

constexpr size_t VMSetter_fieldIndex = 10;
constexpr size_t VMSetter_argIndex = 12;
constexpr size_t VMSetter_numberOfArguments = 14;
constexpr size_t VMSetter_size = 16;

/** VMPrimitive and VMSafe*Primitive: a Prim, which is just a routine. */
constexpr size_t VMPrimitive_prim = 10;
constexpr size_t VMPrimitive_size = 12;

constexpr size_t VMEvaluationPrimitive_numberOfArguments = 10;
constexpr size_t VMEvaluationPrimitive_size = 12;

/** Small integers are tagged, so they have 15 bits. */
constexpr long smallIntMin = -(1L << 14);
constexpr long smallIntMax = (1L << 14) - 1;

constexpr size_t PaddedSize(size_t n) {
    return (n + word - 1) & ~(word - 1);
}
}  // namespace FructusLayout

class VMArray;

/** Friend of the VM object classes, to read and check their fields. */
class FructusImage {
public:
#ifdef FRUCTUS
    /**
     * Set the well-known objects and classes, and register the image's
     * symbols and globals. Before anything looks up a symbol, so that the
     * image's symbols are the ones that are found.
     */
    static void Load();

    /** The arguments of the program, from the host. */
    static VMArray* Arguments();

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
