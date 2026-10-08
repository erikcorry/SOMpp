/*
 * Writes the class library for fructus as assembler, see vm/FructusImage.h.
 *
 * Runs on the host, after the classes were compiled, and writes each object
 * field by field at the offsets in FructusLayout, with references between
 * objects as symbolic address constants for the linker.
 */

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../interpreter/SendSite.h"
#include "../interpreter/bytecodes.h"
#include "../primitives/FructusPrimitives.h"
#include "../vm/FructusImage.h"
#include "../vm/Globals.h"
#include "../vm/Print.h"
#include "../vm/Symbols.h"
#include "../vm/IsValidObject.h"
#include "../vm/Universe.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/Signature.h"
#include "../vmobjects/VMArray.h"
#include "../vmobjects/VMBigInteger.h"
#include "../vmobjects/VMClass.h"
#include "../vmobjects/VMEvaluationPrimitive.h"
#include "../vmobjects/VMInteger.h"
#include "../vmobjects/VMMethod.h"
#include "../vmobjects/VMObject.h"
#include "../vmobjects/VMPrimitive.h"
#include "../vmobjects/VMString.h"
#include "../vmobjects/VMSymbol.h"
#include "../vmobjects/VMTrivialMethod.h"
#include "SourcecodeCompiler.h"

namespace L = FructusLayout;

namespace {

/** Classes fructus has no use for, which are left out of the image. */
const char* const unsupportedClasses[] = {"Double"};

/** Classes the VM itself looks up, besides the system classes. */
const char* const vmClasses[] = {"Block",  "Block1", "Block2", "Block3",
                                 "Block4", "True",   "False",  "System"};

/** An entry of FRUCTUS_PRIMITIVES. */
struct PrimitiveSpec {
    const char* className;
    bool classSide;
    const char* selector;
    const char* kind;
    const char* routine;
};

#define FRUCTUS_PRIMITIVE_SPEC(cls, classSide, selector, kind, routine) \
    {#cls, classSide, selector, #kind, #routine},
const PrimitiveSpec fructusPrimitives[] = {
    FRUCTUS_PRIMITIVES(FRUCTUS_PRIMITIVE_SPEC)};
#undef FRUCTUS_PRIMITIVE_SPEC

/** The mangled name of the VM class of a primitive of the given kind. */
const char* PrimitiveClass(const char* kind) {
    if (strcmp(kind, "Frame") == 0) {
        return "11VMPrimitive";
    }
    if (strcmp(kind, "Unary") == 0) {
        return "20VMSafeUnaryPrimitive";
    }
    if (strcmp(kind, "Binary") == 0) {
        return "21VMSafeBinaryPrimitive";
    }
    if (strcmp(kind, "Ternary") == 0) {
        return "22VMSafeTernaryPrimitive";
    }
    ErrorPrintf("Error writing the fructus image: unknown primitive kind %s\n",
                kind);
    Quit(ERR_FAIL);
}

/** Letters and digits stay, `_` doubles, anything else is _<hex>. */
std::string Mangle(const char* chars, size_t length) {
    std::string result;
    for (size_t i = 0; i < length; i++) {
        auto const c = (unsigned char)chars[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            result += (char)c;
        } else if (c == '_') {
            result += "__";
        } else {
            static const char* const hex = "0123456789abcdef";
            result += '_';
            result += hex[c >> 4U];
            result += hex[c & 15U];
        }
    }
    return result;
}

std::string Mangle(const VMString* str) {
    return Mangle(str->GetRawChars(), str->GetStringLength());
}

std::string SymbolLabel(const VMSymbol* sym) {
    return "som_sym_" + Mangle(sym);
}

/** The label of the object a #symbol in the program is, see FructusImage.h. */
std::string SymbolObjectLabel(const VMSymbol* sym) {
    return "som_symbol_" + Mangle(sym);
}

/** A string as a gas string literal. */
std::string Quote(const char* chars, size_t length) {
    std::string result = "\"";
    for (size_t i = 0; i < length; i++) {
        auto const c = (unsigned char)chars[i];
        if (c == '"' || c == '\\') {
            result += '\\';
            result += (char)c;
        } else if (c >= ' ' && c < 127) {
            result += (char)c;
        } else {
            char octal[5];
            snprintf(octal, sizeof(octal), "\\%03o", c);
            result += octal;
        }
    }
    return result + "\"";
}

/** A string for a comment, which ends at the end of the line. */
std::string Printable(const char* chars, size_t length) {
    std::string result;
    for (size_t i = 0; i < length; i++) {
        auto const c = (unsigned char)chars[i];
        result += (c >= ' ' && c < 127) ? (char)c : '?';
    }
    return result;
}

std::string Printable(const VMString* str) {
    return Printable(str->GetRawChars(), str->GetStringLength());
}

std::string Vtable(const char* mangledClassName) {
    return std::string("_ZTV") + mangledClassName + "+" +
           std::to_string(L::vtableOffset);
}

/**
 * The signature of `inv` in the image. The parser names a block's method
 * $blockMethod@<line>, which only shows in stack traces, so instead the
 * blocks with the same number of arguments share the signature of their
 * #value, and the image has no symbol for each block.
 */
VMSymbol* ImageSignature(VMInvokable* inv) {
    VMSymbol* sig = inv->GetSignature();
    if (dynamic_cast<VMMethod*>(inv) == nullptr ||
        sig->GetStdString().rfind("$blockMethod@", 0) != 0) {
        return sig;
    }
    // value, value:, value:with:, ... with the block itself an argument
    std::string selector = "value";
    for (uint8_t i = 1; i < inv->GetNumberOfArguments(); i++) {
        selector += i == 1 ? ":" : "with:";
    }
    return SymbolFor(selector);
}

/** Methods are only for dispatch, the program can't have them. */
bool IsInvokable(vm_oop_t obj) {
    return !IS_TAGGED(obj) &&
           dynamic_cast<VMInvokable*>(AS_OBJ(obj)) != nullptr;
}

std::string Describe(vm_oop_t obj) {
    auto* o = AS_OBJ(obj);
    if (auto* sym = dynamic_cast<VMSymbol*>(o)) {
        return "the symbol #" + Printable(sym);
    }
    return "a " + Printable(o->GetClass()->GetName());
}

[[noreturn]] void Fail(const std::string& message) {
    ErrorPrintf("Error writing the fructus image: %s\n", message.c_str());
    Quit(ERR_FAIL);
}

/**
 * Writes the fields of one object in order, padding the gaps, and checks
 * that each one is at the offset FructusLayout gives it.
 */
class ObjectWriter {
public:
    ObjectWriter(std::ostream& out, std::string label)
        : out(out), label(std::move(label)) {}

    void Word(size_t offset, const std::string& value,
              const std::string& comment) {
        at(offset);
        out << "\t.short\t" << value << "\t; " << comment << "\n";
        pos += L::word;
    }

    void Word(size_t offset, int64_t value, const std::string& comment) {
        Word(offset, std::to_string(value), comment);
    }

    void Byte(size_t offset, uint8_t value, const std::string& comment) {
        at(offset);
        out << "\t.byte\t" << (unsigned)value << "\t; " << comment << "\n";
        pos += 1;
    }

    /** The address of this object plus `offset`. */
    [[nodiscard]] std::string Self(size_t offset = 0) const {
        return offset == 0 ? label : label + "+" + std::to_string(offset);
    }

    void Raw(size_t offset, size_t length, const std::string& line) {
        at(offset);
        out << line;
        pos += length;
    }

    void End(size_t size) {
        at(size);
        if (pos != size) {
            Fail("object " + label + " has " + std::to_string(pos) +
                 " bytes, expected " + std::to_string(size));
        }
    }

    [[nodiscard]] size_t Pos() const { return pos; }

private:
    void at(size_t offset) {
        if (offset < pos) {
            Fail("field of " + label + " at offset " + std::to_string(offset) +
                 " overlaps the previous one, which ends at " +
                 std::to_string(pos));
        }
        if (offset > pos) {
            out << "\t.zero\t" << (offset - pos) << "\n";
            pos = offset;
        }
    }

    std::ostream& out;
    std::string label;
    size_t pos = 0;
};

}  // namespace

/**
 * One .s file: a class and its metaclass, or the universe. Objects that are
 * not classes are local to the file that first refers to them. A selector
 * is a number, which _selectors.s gives it, see VMSymbol.h, and a #symbol
 * the program has is an object in _universe.s.
 */
class FructusImage::AsmFile {
public:
    AsmFile(ImageWriter& image, const std::string& path,
            const std::string& title);

    /** Write an object with a global label. */
    void WriteGlobal(AbstractVMObject* obj, const std::string& label);
    /** Write the object for a #symbol, see ImageWriter::writeSymbols(). */
    void WriteSymbolObject(VMSymbol* sym, const std::string& label);

    /** Write the local objects that were referred to. */
    void Finish();

    /** The file has the methods of `cls` and its metaclass. */
    void SetOwner(VMClass* cls) { owner = cls; }

    /**
     * The assembler expression for a reference to `obj`, which the program
     * can have: a symbol is the object for it, not its selector.
     */
    std::string Ref(vm_oop_t obj);
    /** The number of a selector or the name of a global. */
    std::string SelectorRef(VMSymbol* sym);
    /** The assembler expression for a tagged integer. */
    std::string IntRef(int64_t value);

    std::ostream& Out() { return out; }

private:
    void write(AbstractVMObject* obj, const std::string& label);
    void writeHeader(ObjectWriter& w, const std::string& vtable,
                     const std::string& comment);
    void writeVMObject(VMObject* obj, const std::string& label);
    void writeString(VMString* str, const std::string& label);
    void writeInteger(VMInteger* integer, const std::string& label);
    void writeInvokableHeader(ObjectWriter& w, VMInvokable* inv,
                              const std::string& vtable,
                              const std::string& comment);
    void writeMethod(VMMethod* method, const std::string& label);
    void writePrimitive(VMPrimitive* prim, const std::string& label);
    void writeEvaluationPrimitive(VMEvaluationPrimitive* prim,
                                  const std::string& label);
    void writeLiteralReturn(VMLiteralReturn* inv, const std::string& label);
    void writeGlobalReturn(VMGlobalReturn* inv, const std::string& label);
    void writeGetter(VMGetter* inv, const std::string& label);
    void writeSetter(VMSetter* inv, const std::string& label);
    // the offset and source line where each line starts
    using Lines = std::vector<std::pair<size_t, uint16_t>>;
    Lines writeBytecodes(ObjectWriter& w, size_t offset, VMMethod* method,
                         const std::vector<uint8_t>& fructusBytecodes);
    void writeDebugInfo();
    /** Write the send at `index`, and return how long it is on fructus. */
    size_t writeSendSite(ObjectWriter& w, size_t offset, VMMethod* method,
                         size_t index);
    std::string describe(VMInvokable* inv);

    ImageWriter& image;
    std::ofstream out;
    std::map<AbstractVMObject*, std::string> localLabels;
    std::map<int64_t, VMInteger*> boxedIntegers;
    std::deque<AbstractVMObject*> pending;
    std::set<AbstractVMObject*> globals;
    VMClass* owner = nullptr;

    /** The bytecodes of a method or block, for writeDebugInfo(). */
    struct DebugMethod {
        std::string name;
        size_t file;        // in debugFiles, from 1
        std::string start;  // the address of the first bytecode
        size_t length;
        Lines lines;
    };
    std::vector<DebugMethod> debugMethods;
    std::vector<std::string> debugFiles;
};

class FructusImage::ImageWriter {
public:
    explicit ImageWriter(std::string dir) : dir(std::move(dir)) {}

    void Write(const std::vector<std::string>& arguments);

    /** The label of the number of a selector, see writeSelectors(). */
    std::string SelectorLabel(VMSymbol* sym) {
        std::string label = SymbolLabel(sym);
        selectors[label] = sym;
        return label;
    }

    /** The label of the object for a #symbol, see writeSymbols(). */
    std::string SymbolObjectRef(VMSymbol* sym) {
        std::string label = SymbolObjectLabel(sym);
        symbolObjects[label] = sym;
        return label;
    }

    /**
     * The superclass of `cls` in the image. A symbol isn't a string on
     * fructus, see writeSymbols(), so Symbol is an Object there.
     */
    VMClass* SuperClass(VMClass* cls) {
        if (cls == load_ptr(symbolClass)) {
            return load_ptr(objectClass);
        }
        if (cls == load_ptr(symbolClass)->GetClass()) {
            return load_ptr(objectClass)->GetClass();
        }
        return static_cast<VMClass*>(cls->GetSuperClass());
    }

    /** The label of a class or metaclass in the image. */
    std::string ClassLabel(VMClass* cls) {
        auto const it = classLabels.find(cls);
        if (it == classLabels.end()) {
            Fail("class " + Printable(cls->GetName()) + " is not in the image");
        }
        return it->second;
    }

    [[nodiscard]] bool IsClass(AbstractVMObject* obj) const {
        return classLabels.find(obj) != classLabels.end();
    }

    /** Whether `obj` is the instanceInvokables of a class. */
    [[nodiscard]] bool IsClassArray(AbstractVMObject* obj) const {
        return classArrays.find(obj) != classArrays.end();
    }

    /**
     * The global label of a method of a class, and the class, or nullptr if
     * `obj` isn't one. Super sends refer to them from other files.
     */
    const std::pair<std::string, VMClass*>* ClassInvokable(
        AbstractVMObject* obj) const {
        auto const it = classInvokables.find(obj);
        return it == classInvokables.end() ? nullptr : &it->second;
    }

    /** The fructus primitive `prim` stands for, or nullptr if there's none. */
    const PrimitiveSpec* PrimitiveFor(VMPrimitive* prim) const {
        auto const it = primitives.find(prim);
        return it == primitives.end() ? nullptr : it->second;
    }

private:
    void collectClasses(const std::vector<std::string>& arguments);
    void checkClosedWorld();
    void checkPushes(VMClass* cls, VMMethod* method);
    void checkClassFields(VMClass* metaclass, VMInvokable* inv,
                          VMMethod* method);
    void installPrimitives(VMClass* cls);
    void addClass(const char* name, bool required);
    void addGlobalsOf(VMClass* cls);
    void addGlobalsOf(VMInvokable* inv);
    void writeClass(VMClass* cls);
    void writeUniverse(const std::vector<std::string>& arguments);
    void writeSymbols(AsmFile& file);
    void writeSelectors();

    std::string dir;
    std::map<std::string, VMSymbol*> selectors;  // by label, for stable numbers
    std::map<std::string, VMSymbol*> symbolObjects;  // by label
    std::vector<VMClass*> classes;
    std::deque<VMClass*> toScan;
    std::map<AbstractVMObject*, std::string> classLabels;
    std::map<VMPrimitive*, const PrimitiveSpec*> primitives;
    std::set<AbstractVMObject*> classArrays;
    std::map<AbstractVMObject*, std::pair<std::string, VMClass*>>
        classInvokables;
};

FructusImage::AsmFile::AsmFile(ImageWriter& image, const std::string& path,
                 const std::string& title)
    : image(image), out(path) {
    if (!out) {
        Fail("can't open " + path);
    }
    out << "; " << title << "\n"
        << "; Written for fructus by SOM++ -asm, see src/vm/FructusImage.h\n\n"
        << "\t.data\n";
}

std::string FructusImage::AsmFile::IntRef(int64_t value) {
    if (value < L::smallIntMin || value > L::smallIntMax) {
        Fail("the integer " + std::to_string(value) +
             " is too big for fructus");
    }
    return std::to_string(value * 2 + 1);
}

std::string FructusImage::AsmFile::Ref(vm_oop_t obj) {
    // an integer, which fructus boxes unless it's a small one, see
    // som_box_int_t, once in each file
    if (IS_TAGGED(obj) || IsVMBigInteger(obj)) {
        int64_t value = 0;
        if (IS_TAGGED(obj)) {
            value = (int64_t)SMALL_INT_VAL(obj);
        } else {
            InfInt const& big = static_cast<VMBigInteger*>(obj)->embeddedInteger;
            if (big < InfInt(INT64_MIN) || big > InfInt(INT64_MAX)) {
                Fail("the integer literal " + big.toString() +
                     " doesn't fit in the 64 bits of fructus");
            }
            value = big.toInt64();
        }
        if (value >= L::smallIntMin && value <= L::smallIntMax) {
            return IntRef(value);
        }
        VMInteger*& box = boxedIntegers[value];
        if (box == nullptr) {
            box = Universe::NewInteger(value);
        }
        obj = box;
    }

    auto* o = AS_OBJ(obj);
    if (o == load_ptr(nilObject)) {
        return "som_nil";
    }
    if (o == load_ptr(trueObject)) {
        return "som_true";
    }
    if (o == load_ptr(falseObject)) {
        return "som_false";
    }
    if (auto* cls = dynamic_cast<VMClass*>(o)) {
        return image.ClassLabel(cls);
    }
    if (auto* sym = dynamic_cast<VMSymbol*>(o)) {
        return image.SymbolObjectRef(sym);
    }

    auto const it = localLabels.find(o);
    if (it != localLabels.end()) {
        return it->second;
    }
    if (const auto* inv = image.ClassInvokable(o)) {
        VMClass* holder = inv->second;
        if (owner == nullptr ||
            (holder != owner && holder != owner->GetClass())) {
            return inv->first;  // in the file of its class
        }
        localLabels[o] = inv->first;
        globals.insert(o);
        pending.push_back(o);
        return inv->first;
    }
    std::string label = ".Lo" + std::to_string(localLabels.size());
    localLabels[o] = label;
    pending.push_back(o);
    return label;
}

std::string FructusImage::AsmFile::SelectorRef(VMSymbol* sym) {
    return image.SelectorLabel(sym);
}

void FructusImage::AsmFile::WriteGlobal(AbstractVMObject* obj, const std::string& label) {
    out << "\n\t.globl\t" << label << "\n";
    write(obj, label);
}

void FructusImage::AsmFile::WriteSymbolObject(VMSymbol* sym,
                                              const std::string& label) {
    out << "\n\t.globl\t" << label << "\n"
        << "\t.balign\t2\n"
        << label << ":\n";
    ObjectWriter w(out, label);
    size_t const size = L::VMObject_size + L::word;
    writeHeader(w, Vtable("8VMObject"), "Symbol #" + Printable(sym));
    w.Word(L::VMObject_totalObjectSize, (int64_t)size, "totalObjectSize");
    w.Word(L::VMObject_numberOfFields, 1, "numberOfFields");
    w.Word(L::VMObject_clazz, image.ClassLabel(load_ptr(symbolClass)),
           "class");
    // its characters, a string of its own
    w.Word(L::VMObject_size, Ref(Universe::NewString(sym->GetStdString())),
           "string");
    w.End(size);
}

void FructusImage::AsmFile::Finish() {
    while (!pending.empty()) {
        AbstractVMObject* obj = pending.front();
        pending.pop_front();
        out << "\n";
        if (globals.count(obj) != 0) {
            out << "\t.globl\t" << localLabels[obj] << "\n";
        }
        write(obj, localLabels[obj]);
    }

    writeDebugInfo();
}

/**
 * DWARF 3 for the methods and blocks, which costs fructus nothing, since
 * the debug sections aren't loaded: a compile unit with a subprogram for
 * each, named like Foo>>#bar:, and a line table, so that addr2line can say
 * where a pc in a stack trace is. Like gcc for fructus, a 4-byte offset is
 * a 2-byte one and 0, since there is no 4-byte relocation.
 */
void FructusImage::AsmFile::writeDebugInfo() {
    if (debugMethods.empty()) {
        return;
    }

    out << "\n\t.section\t.debug_abbrev,\"\",@progbits\n"
        << ".Ldebug_abbrev:\n"
        // 1: DW_TAG_compile_unit, with children
        << "\t.byte\t1, 0x11, 1\n"
        // DW_AT_name string, DW_AT_low_pc addr, DW_AT_ranges data4,
        // DW_AT_stmt_list data4
        << "\t.byte\t0x03, 0x08, 0x11, 0x01, 0x55, 0x06, 0x10, 0x06, 0, 0\n"
        // 2: DW_TAG_subprogram, without
        << "\t.byte\t2, 0x2e, 0\n"
        // DW_AT_name string, DW_AT_low_pc addr, DW_AT_high_pc addr
        << "\t.byte\t0x03, 0x08, 0x11, 0x01, 0x12, 0x01, 0, 0\n"
        << "\t.byte\t0\n";

    out << "\n\t.section\t.debug_info,\"\",@progbits\n"
        << "\t.short\t.Ldebug_info_end-.Ldebug_info_start, 0\n"
        << ".Ldebug_info_start:\n"
        << "\t.short\t3\n"
        << "\t.short\t.Ldebug_abbrev, 0\n"
        << "\t.byte\t" << L::word << "\n"
        << "\t.byte\t1\n"
        << "\t.string\t" << Quote(debugFiles[0].data(), debugFiles[0].size())
        << "\n"
        << "\t.short\t0\n"
        << "\t.short\t.Ldebug_ranges, 0\n"
        << "\t.short\t.Ldebug_line, 0\n";
    for (DebugMethod const& m : debugMethods) {
        out << "\t.byte\t2\n"
            << "\t.string\t" << Quote(m.name.data(), m.name.size()) << "\n"
            << "\t.short\t" << m.start << ", " << m.start << "+" << m.length
            << "\n";
    }
    out << "\t.byte\t0\n"
        << ".Ldebug_info_end:\n";

    out << "\n\t.section\t.debug_ranges,\"\",@progbits\n"
        << ".Ldebug_ranges:\n";
    for (DebugMethod const& m : debugMethods) {
        out << "\t.short\t" << m.start << ", " << m.start << "+" << m.length
            << "\n";
    }
    out << "\t.short\t0, 0\n";

    out << "\n\t.section\t.debug_line,\"\",@progbits\n"
        << ".Ldebug_line:\n"
        << "\t.short\t.Ldebug_line_end-.Ldebug_line_start, 0\n"
        << ".Ldebug_line_start:\n"
        << "\t.short\t3\n"
        << "\t.short\t.Ldebug_line_program-.Ldebug_line_header, 0\n"
        << ".Ldebug_line_header:\n"
        // minimum_instruction_length, default_is_stmt, line_base, line_range,
        // opcode_base, and the lengths of the standard opcodes
        << "\t.byte\t1, 1, -5, 14, 13\n"
        << "\t.byte\t0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1\n"
        << "\t.byte\t0\n";  // no include_directories
    for (std::string const& file : debugFiles) {
        out << "\t.string\t" << Quote(file.data(), file.size()) << "\n"
            << "\t.byte\t0, 0, 0\n";
    }
    out << "\t.byte\t0\n"
        << ".Ldebug_line_program:\n";
    for (DebugMethod const& m : debugMethods) {
        // a sequence for each, which starts at line 1
        out << "\t.byte\t4, " << m.file << "\t; " << m.name << "\n";
        int64_t line = 1;
        for (auto const& [offset, l] : m.lines) {
            // DW_LNE_set_address, DW_LNS_advance_line, DW_LNS_copy
            out << "\t.byte\t0, 3, 2\n"
                << "\t.short\t" << m.start << "+" << offset << "\n"
                << "\t.byte\t3\n"
                << "\t.sleb128\t" << ((int64_t)l - line) << "\n"
                << "\t.byte\t1\n";
            line = l;
        }
        // DW_LNE_end_sequence after the last bytecode
        out << "\t.byte\t0, 3, 2\n"
            << "\t.short\t" << m.start << "+" << m.length << "\n"
            << "\t.byte\t0, 1, 1\n";
    }
    out << ".Ldebug_line_end:\n";
}

void FructusImage::AsmFile::write(AbstractVMObject* obj, const std::string& label) {
    out << "\t.balign\t2\n" << label << ":\n";

    // the most derived classes first
    if (auto* str = dynamic_cast<VMString*>(obj)) {
        writeString(str, label);
    } else if (auto* integer = dynamic_cast<VMInteger*>(obj)) {
        writeInteger(integer, label);
    } else if (auto* method = dynamic_cast<VMMethod*>(obj)) {
        writeMethod(method, label);
    } else if (auto* prim = dynamic_cast<VMPrimitive*>(obj)) {
        writePrimitive(prim, label);
    } else if (auto* eval = dynamic_cast<VMEvaluationPrimitive*>(obj)) {
        writeEvaluationPrimitive(eval, label);
    } else if (auto* lit = dynamic_cast<VMLiteralReturn*>(obj)) {
        writeLiteralReturn(lit, label);
    } else if (auto* glob = dynamic_cast<VMGlobalReturn*>(obj)) {
        writeGlobalReturn(glob, label);
    } else if (auto* getter = dynamic_cast<VMGetter*>(obj)) {
        writeGetter(getter, label);
    } else if (auto* setter = dynamic_cast<VMSetter*>(obj)) {
        writeSetter(setter, label);
    } else if (auto* vmObj = dynamic_cast<VMObject*>(obj)) {
        writeVMObject(vmObj, label);
    } else {
        Fail("can't write an object of class " +
             Printable(obj->GetClass()->GetName()) +
             " for fructus, it has none");
    }
}

void FructusImage::AsmFile::writeHeader(ObjectWriter& w, const std::string& vtable,
                          const std::string& comment) {
    w.Word(0, vtable, comment);
}

/** The fields of every class on the host, see VMClass::VMClassNumberOfFields. */
const char* const classFields[] = {"name", "instanceFields",
                                   "instanceInvokables", "superClass"};
constexpr size_t nameField = 0;
constexpr size_t instanceFieldsField = 1;
constexpr size_t superClassField = 3;
static_assert(L::VMClass_numberOfInstanceFields ==
                  L::VMObject_size + (instanceFieldsField - 1) * L::word,
              "fructus classes have no name");
static_assert(L::VMClass_superClass ==
                  L::VMObject_size + (superClassField - 1) * L::word,
              "fructus classes have no name");

/**
 * Whether the receivers of `holder`'s methods are classes: Class, and its
 * subclasses, which the metaclasses are.
 */
static bool IsClassSide(VMClass* holder) {
    for (VMObject* c = holder; c != load_ptr(nilObject);
         c = static_cast<VMClass*>(c)->GetSuperClass()) {
        if (c == load_ptr(classClass)) {
            return true;
        }
    }
    return false;
}

/** Whether `bc` has the index of a field after it. */
static bool HasFieldOperand(uint8_t bc) {
    return bc == BC_PUSH_FIELD || bc == BC_POP_FIELD || bc == BC_INC_FIELD ||
           bc == BC_INC_FIELD_PUSH;
}

/** Whether `bc` is for one of the first fields, which it has no index of. */
static bool IsFixedFieldBytecode(uint8_t bc) {
    return bc == BC_PUSH_FIELD_0 || bc == BC_PUSH_FIELD_1 ||
           bc == BC_POP_FIELD_0 || bc == BC_POP_FIELD_1 ||
           bc == BC_RETURN_FIELD_0 || bc == BC_RETURN_FIELD_1 ||
           bc == BC_RETURN_FIELD_2;
}

/**
 * The index on fructus of field `index` of an instance of `holder`. A class
 * has no name there, its first field on the host, so the other fields are
 * one lower. The class side can't have the name, see checkClassFields().
 */
static size_t FructusFieldIndex(VMClass* holder, size_t index) {
    if (holder == nullptr || !IsClassSide(holder)) {
        return index;
    }
    if (index == nameField) {
        Fail("the name of a class isn't a field on fructus");
    }
    return index - 1;
}

/** VMObject, VMClass, VMArray: a header, and fields that are all oops. */
void FructusImage::AsmFile::writeVMObject(VMObject* obj, const std::string& label) {
    ObjectWriter w(out, label);
    bool const isClass = dynamic_cast<VMClass*>(obj) != nullptr;
    // a class has no name on fructus, see VMClass.h
    size_t const numberOfFields = obj->GetNumberOfFields() - (isClass ? 1 : 0);
    size_t const size = L::VMObject_size + numberOfFields * L::word;

    std::string vtable;
    std::string comment;
    if (auto* cls = dynamic_cast<VMClass*>(obj)) {
        vtable = Vtable("7VMClass");
        comment = "Class " + Printable(cls->GetName());
    } else if (dynamic_cast<VMArray*>(obj) != nullptr) {
        vtable = Vtable("7VMArray");
        comment = "Array";
    } else if (typeid(*obj) == typeid(VMObject)) {
        vtable = Vtable("8VMObject");
        comment = "instance of " + Printable(obj->GetClass()->GetName());
    } else {
        Fail("can't write an object of class " +
             Printable(obj->GetClass()->GetName()) + " for fructus");
    }

    writeHeader(w, vtable, comment);
    w.Word(L::VMObject_totalObjectSize, (int64_t)size, "totalObjectSize");
    w.Word(L::VMObject_numberOfFields, (int64_t)numberOfFields,
           "numberOfFields");
    w.Word(L::VMObject_clazz, Ref(obj->GetClass()), "class");

    // the program can get at the other objects, and at the class-side
    // fields of classes
    bool const isInternal = image.IsClassArray(obj);
    for (size_t i = isClass ? nameField + 1 : 0; i < obj->GetNumberOfFields();
         i++) {
        bool const isClassField = isClass && i < 4;
        size_t const offset =
            L::VMObject_size + (isClass ? i - 1 : i) * L::word;
        vm_oop_t field = obj->GetField(i);
        if (isClass && i == superClassField) {
            field = image.SuperClass(static_cast<VMClass*>(obj));
        }
        if (isClass && i == instanceFieldsField) {
            // fructus has no field names, just how many, see VMClass.h
            w.Word(offset,
                   IntRef((int64_t) static_cast<VMClass*>(obj)
                              ->GetNumberOfInstanceFields()),
                   "numberOfInstanceFields");
            continue;
        }
        if (!isClassField && !isInternal && IsInvokable(field)) {
            Fail("the program could get at " + Describe(field) + " in " +
                 comment);
        }
        w.Word(offset, Ref(field),
               isClassField ? classFields[i] : "field " + std::to_string(i));
    }
    w.End(size);

    // The program can store objects of the heap into it, so the collector
    // walks it, see FructusImage::WalkGlobals()
    if (!isInternal) {
        out << "\t.pushsection\tsom_roots,\"a\",@progbits\n"
            << "\t.balign\t2\n"
            << "\t.short\t" << label << "\n"
            << "\t.popsection\n";
    }
}

void FructusImage::AsmFile::writeString(VMString* str, const std::string& label) {
    ObjectWriter w(out, label);
    size_t const length = str->GetStringLength();
    writeHeader(w, Vtable("8VMString"), "String '" + Printable(str) + "'");
    w.Word(L::VMString_length, (int64_t)length, "length");
    w.Word(L::VMString_chars, w.Self(L::VMString_size), "chars");
    w.Raw(L::VMString_size, length,
          "\t.ascii\t" + Quote(str->GetRawChars(), length) + "\n");
    w.End(L::VMString_size + L::PaddedSize(length));
}

/** An integer that isn't a small one on fructus, with 64 bits. */
void FructusImage::AsmFile::writeInteger(VMInteger* integer,
                                         const std::string& label) {
    ObjectWriter w(out, label);
    auto const value = (int64_t)integer->GetEmbeddedInteger();
    writeHeader(w, Vtable("9VMInteger"), "Integer " + std::to_string(value));
    // little endian, a word at a time
    for (size_t i = 0; i < L::VMInteger_valueSize; i += L::word) {
        auto const bits = (uint16_t)((uint64_t)value >> (i * 8));
        w.Word(L::VMInteger_value + i, (int64_t)bits,
               i == 0 ? "value" : "");
    }
    w.End(L::VMInteger_size);
}

std::string FructusImage::AsmFile::describe(VMInvokable* inv) {
    VMClass* holder = inv->GetHolder();
    std::string const holderName =
        holder == nullptr ? "?" : Printable(holder->GetName());
    return holderName + ">>#" + Printable(inv->GetSignature());
}

void FructusImage::AsmFile::writeInvokableHeader(ObjectWriter& w, VMInvokable* inv,
                                   const std::string& vtable,
                                   const std::string& comment) {
    writeHeader(w, vtable, comment + " " + describe(inv));
    w.Word(L::VMInvokable_signature, SelectorRef(ImageSignature(inv)),
           "signature");
}

void FructusImage::AsmFile::writeMethod(VMMethod* method, const std::string& label) {
    ObjectWriter w(out, label);
    // the words are shorter on fructus, and writeBytecodes() writes them
    std::vector<uint8_t> const fructusBytecodes =
        method->ResizeWords(L::word, [](void* /*word*/, uint8_t* /*out*/) {});
    size_t const bcLength = fructusBytecodes.size();

    if (method->numberOfLocals > UINT8_MAX ||
        method->maximumNumberOfStackElements > UINT8_MAX) {
        Fail(describe(method) + " has too many locals or too deep a stack");
    }

    writeInvokableHeader(w, method, Vtable("8VMMethod"), "Method");
    w.Word(L::VMMethod_bcLength, (int64_t)bcLength, "bcLength");
    w.Byte(L::VMMethod_numberOfLocals, (uint8_t)method->numberOfLocals,
           "numberOfLocals");
    w.Byte(L::VMMethod_maximumNumberOfStackElements,
           (uint8_t)method->maximumNumberOfStackElements,
           "maximumNumberOfStackElements");
    w.Byte(L::VMMethod_numberOfArguments, method->numberOfArguments,
           "numberOfArguments");
    Lines lines = writeBytecodes(w, L::VMMethod_size, method, fructusBytecodes);
    w.End(L::PaddedSize(L::VMMethod_size + bcLength));

    // the debug info has the method's file, and the lines, which the host
    // has for its bytecodes
    VMClass* holder = method->GetHolder();
    std::string className =
        holder == nullptr ? "" : holder->GetName()->GetStdString();
    std::string const metaclassSuffix = " class";
    if (className.size() > metaclassSuffix.size() &&
        className.compare(className.size() - metaclassSuffix.size(),
                          metaclassSuffix.size(), metaclassSuffix) == 0) {
        className.erase(className.size() - metaclassSuffix.size());
    }
    std::string file = SourcecodeCompiler::SourceFile(className);
    if (file.empty()) {
        file = className + ".som";
    }
    auto it = std::find(debugFiles.begin(), debugFiles.end(), file);
    if (it == debugFiles.end()) {
        it = debugFiles.insert(debugFiles.end(), file);
    }
    debugMethods.push_back({describe(method),
                            (size_t)(it - debugFiles.begin()) + 1,
                            w.Self(L::VMMethod_size), bcLength,
                            std::move(lines)});
}

/**
 * The bytecodes as ResizeWords() made them for fructus, with the words, which
 * are at the same bytecode as on the host.
 */
FructusImage::AsmFile::Lines FructusImage::AsmFile::writeBytecodes(
    ObjectWriter& w, size_t offset, VMMethod* method,
    const std::vector<uint8_t>& fructusBytecodes) {
    size_t const bcLength = method->bcLength;
    Lines lines;
    size_t i = 0;
    size_t n = 0;
    while (i < bcLength) {
        uint8_t const bc = method->GetBytecode(i);
        uint16_t const sourceLine = method->GetLine(i);
        if (sourceLine != 0 &&
            (lines.empty() || lines.back().second != sourceLine)) {
            lines.emplace_back(n, sourceLine);
        }
        size_t const length = Bytecode::GetBytecodeLength(bc);
        if (length == 0 || i + length > bcLength) {
            Fail("bad bytecode " + std::to_string(bc) + " in " +
                 describe(method));
        }
        if (IsFixedFieldBytecode(bc) && method->GetHolder() != nullptr &&
            IsClassSide(method->GetHolder())) {
            // the first fields of a class are the VM's, see
            // checkClassFields()
            Fail(describe(method) + " uses a field of the class that only "
                 "the VM can");
        }
        if (IsSendBytecode(bc)) {
            n += writeSendSite(w, offset + n, method, i);
            i += length;
            continue;
        }
        // the bytes, and the word of a PUSH_CONSTANT, PUSH_BLOCK or
        // PUSH_GLOBAL
        bool const hasWord = GetNumberOfWords(bc) != 0;
        size_t const numBytes = hasWord ? 1 : length;
        size_t const fructusLength = hasWord ? 1 + L::word : length;
        std::ostringstream line;
        line << "\t.byte\t";
        for (size_t j = 0; j < numBytes; j++) {
            unsigned byte = fructusBytecodes[n + j];
            if (j == 1 && HasFieldOperand(bc)) {
                byte = FructusFieldIndex(method->GetHolder(), byte);
            }
            line << (j == 0 ? "" : ", ") << byte;
        }
        std::string name = Bytecode::GetBytecodeName(fructusBytecodes[n]);
        name.erase(name.find_last_not_of(' ') + 1);
        line << "\t; " << name;
        if (hasWord) {
            vm_oop_t word = method->GetConstant(i);
            if (bc == BC_PUSH_GLOBAL) {
                line << " " << Printable((VMSymbol*)word);
            } else if (bc == BC_PUSH_CONSTANT) {
                line << " "
                     << (IS_TAGGED(word)
                             ? std::to_string((int64_t)SMALL_INT_VAL(word))
                             : Describe(word));
            }
            line << "\n\t.short\t"
                 << (bc == BC_PUSH_GLOBAL ? SelectorRef((VMSymbol*)word)
                                          : Ref(word));
        }
        line << "\n";
        w.Raw(offset + n, fructusLength, line.str());
        i += length;
        n += fructusLength;
    }
    if (n != fructusBytecodes.size()) {
        Fail("the bytecodes of " + describe(method) + " don't add up");
    }
    return lines;
}

/**
 * A send, unlinked, see SendSite.h. A super send is linked here, since the
 * world is closed and it doesn't depend on the receiver.
 */
size_t FructusImage::AsmFile::writeSendSite(ObjectWriter& w, size_t offset,
                                            VMMethod* method, size_t index) {
    uint8_t const bc = method->GetBytecode(index);
    VMSymbol* selector = method->GetSendSelector(index);
    std::string name = Bytecode::GetBytecodeName(bc);
    name.erase(name.find_last_not_of(' ') + 1);

    std::string target = SelectorRef(selector);
    std::string receiverClass = "0";
    std::string comment = name + " #" + Printable(selector);
    if (bc == BC_SUPER_SEND) {
        VMClass* holder = method->GetHolder();
        VMClass* super = image.SuperClass(holder);
        VMInvokable* inv = super->LookupInvokable(selector);
        if (inv == nullptr) {
            // the invokables have no holder to find the superclass with at
            // run time, see Interpreter::doSuperSend()
            Fail(describe(method) + " sends #" + Printable(selector) +
                 " to super, which " + Printable(super->GetName()) +
                 " doesn't understand");
        }
        target = Ref(inv);
        receiverClass = Ref(super);
        comment += " in " + Printable(super->GetName());
    }

    // a unary send has no number of arguments
    std::ostringstream line;
    line << "\t.byte\t" << (unsigned)bc;
    if (bc != BC_SEND_1) {
        line << ", "
             << (unsigned)method->GetBytecode(index +
                                              SendSite::numberOfArguments);
    }
    line << "\t; " << comment << "\n"
         << "\t.short\t" << target << ", " << receiverClass << "\n";
    size_t const length =
        bc == BC_SEND_1 ? L::SendSite_unaryLength : L::SendSite_length;
    w.Raw(offset, length, line.str());
    return length;
}

void FructusImage::AsmFile::writePrimitive(VMPrimitive* prim,
                                           const std::string& label) {
    ObjectWriter w(out, label);
    const PrimitiveSpec* spec = image.PrimitiveFor(prim);
    if (spec == nullptr && !prim->IsEmpty()) {
        Fail("the primitive " + describe(prim) +
             " is bound on the host, and fructus doesn't have it");
    }
    // without a spec, a primitive fructus doesn't have, which fails when
    // it's called
    std::string const kind = spec == nullptr ? "Frame" : spec->kind;
    writeInvokableHeader(w, prim, Vtable(PrimitiveClass(kind.c_str())),
                         spec == nullptr ? "Missing primitive"
                                         : kind + " primitive");
    w.Word(L::VMPrimitive_prim,
           spec == nullptr ? "som_empty_primitive" : spec->routine,
           "routine");
    if (kind == "Frame") {
        // a VMPrimitive, whose selector can't tell
        w.Byte(L::VMPrimitive_numberOfArguments,
               Signature::GetNumberOfArguments(prim->GetSignature()),
               "numberOfArguments");
        w.End(L::VMPrimitive_size);
    } else {
        w.End(L::VMSafePrimitive_size);
    }
}

void FructusImage::AsmFile::writeEvaluationPrimitive(
    VMEvaluationPrimitive* prim, const std::string& label) {
    ObjectWriter w(out, label);
    writeInvokableHeader(w, prim, Vtable("21VMEvaluationPrimitive"),
                         "EvaluationPrimitive");
    w.Byte(L::VMEvaluationPrimitive_numberOfArguments,
           prim->numberOfArguments, "numberOfArguments");
    w.End(L::VMEvaluationPrimitive_size);
}

void FructusImage::AsmFile::writeLiteralReturn(VMLiteralReturn* inv,
                                 const std::string& label) {
    ObjectWriter w(out, label);
    if (IsInvokable(load_ptr(inv->literal))) {
        Fail(describe(inv) + " answers " + Describe(load_ptr(inv->literal)) +
             ", but the program can't have it");
    }
    writeInvokableHeader(w, inv, Vtable("15VMLiteralReturn"),
                         "LiteralReturn");
    w.Word(L::VMLiteralReturn_literal, Ref(load_ptr(inv->literal)),
           "literal");
    w.Byte(L::VMLiteralReturn_numberOfArguments, inv->numberOfArguments,
           "numberOfArguments");
    w.End(L::VMLiteralReturn_size);
}

void FructusImage::AsmFile::writeGlobalReturn(VMGlobalReturn* inv,
                                const std::string& label) {
    ObjectWriter w(out, label);
    writeInvokableHeader(w, inv, Vtable("14VMGlobalReturn"), "GlobalReturn");
    w.Word(L::VMGlobalReturn_globalName, SelectorRef(load_ptr(inv->globalName)),
           "globalName");
    w.Byte(L::VMGlobalReturn_numberOfArguments, inv->numberOfArguments,
           "numberOfArguments");
    w.End(L::VMGlobalReturn_size);
}

void FructusImage::AsmFile::writeGetter(VMGetter* inv, const std::string& label) {
    ObjectWriter w(out, label);
    writeInvokableHeader(w, inv, Vtable("8VMGetter"), "Getter");
    w.Word(L::VMGetter_fieldIndex,
           (int64_t)FructusFieldIndex(inv->GetHolder(), inv->fieldIndex),
           "fieldIndex");
    w.Byte(L::VMGetter_numberOfArguments, inv->numberOfArguments,
           "numberOfArguments");
    w.End(L::VMGetter_size);
}

void FructusImage::AsmFile::writeSetter(VMSetter* inv, const std::string& label) {
    ObjectWriter w(out, label);
    writeInvokableHeader(w, inv, Vtable("8VMSetter"), "Setter");
    w.Word(L::VMSetter_fieldIndex,
           (int64_t)FructusFieldIndex(inv->GetHolder(), inv->fieldIndex),
           "fieldIndex");
    w.Word(L::VMSetter_argIndex, (int64_t)inv->argIndex, "argIndex");
    w.Byte(L::VMSetter_numberOfArguments, inv->numberOfArguments,
           "numberOfArguments");
    w.End(L::VMSetter_size);
}

void FructusImage::ImageWriter::addClass(const char* name, bool required) {
    for (const char* unsupported : unsupportedClasses) {
        if (strcmp(name, unsupported) == 0) {
            return;
        }
    }

    VMSymbol* sym = SymbolFor(name);
    VMClass* cls = Universe::LoadClass(sym);
    if ((vm_oop_t)cls == load_ptr(nilObject) || IsClass(cls)) {
        if (required && !IsClass(cls)) {
            Fail(std::string("can't find the class ") + name);
        }
        return;
    }
    // a global that isn't a class, like `system'
    if (dynamic_cast<VMClass*>((AbstractVMObject*)cls) == nullptr) {
        return;
    }

    std::string const mangled = Mangle(cls->GetName());
    classLabels[cls] = "som_class_" + mangled;
    classLabels[cls->GetClass()] = "som_meta_" + mangled;
    classes.push_back(cls);
    toScan.push_back(cls);

    if (cls->HasSuperClass()) {
        auto* superClass = static_cast<VMClass*>(cls->GetSuperClass());
        addClass(superClass->GetName()->GetStdString().c_str(), true);
    }
}

void FructusImage::ImageWriter::addGlobalsOf(VMInvokable* inv) {
    if (auto* glob = dynamic_cast<VMGlobalReturn*>(inv)) {
        VMSymbol* name = load_ptr(glob->globalName);
        addClass(name->GetStdString().c_str(), false);
        return;
    }
    auto* method = dynamic_cast<VMMethod*>(inv);
    if (method == nullptr) {
        return;
    }
    for (size_t i = 0; i < method->bcLength;
         i += Bytecode::GetBytecodeLength(method->GetBytecode(i))) {
        if (method->GetBytecode(i) == BC_PUSH_GLOBAL) {
            auto* name = static_cast<VMSymbol*>(method->GetConstant(i));
            addClass(name->GetStdString().c_str(), false);
        } else if (method->GetBytecode(i) == BC_PUSH_BLOCK) {
            addGlobalsOf(static_cast<VMInvokable*>(method->GetConstant(i)));
        }
    }
}

void FructusImage::ImageWriter::addGlobalsOf(VMClass* cls) {
    for (VMClass* c : {cls, cls->GetClass()}) {
        size_t const n = c->GetNumberOfInstanceInvokables();
        for (size_t i = 0; i < n; i++) {
            addGlobalsOf(c->GetInstanceInvokable(i));
        }
    }
}

void FructusImage::ImageWriter::collectClasses(const std::vector<std::string>& arguments) {
    for (GCClass* cls :
         {objectClass, classClass, metaClassClass, nilClass, arrayClass,
          vectorClass, methodClass, symbolClass, integerClass, primitiveClass,
          stringClass}) {
        addClass(load_ptr(cls)->GetName()->GetStdString().c_str(), true);
    }
    for (const char* name : vmClasses) {
        addClass(name, strcmp(name, "Block4") != 0);
    }
    if (!arguments.empty()) {
        addClass(arguments[0].c_str(), true);
    }

    while (!toScan.empty()) {
        VMClass* cls = toScan.front();
        toScan.pop_front();
        addGlobalsOf(cls);
    }
}

/**
 * Put the fructus primitives of `cls` in place, as placeholders that are
 * written as the primitives. They replace the methods with the same
 * selector, whatever those are.
 */
void FructusImage::ImageWriter::installPrimitives(VMClass* cls) {
    std::string const name = cls->GetName()->GetStdString();
    for (const PrimitiveSpec& spec : fructusPrimitives) {
        if (name != spec.className) {
            continue;
        }
        VMClass* holder = spec.classSide ? cls->GetClass() : cls;
        auto* prim = static_cast<VMPrimitive*>(VMPrimitive::GetEmptyPrimitive(
            SymbolFor(spec.selector), spec.classSide));
        holder->InstallPrimitive(prim, 0, false);
        primitives[prim] = &spec;
    }

    // Block1 is the class of blocks with only the receiver as argument, and
    // so on, see FructusImage::BlockClass()
    for (int n = 1; n <= maxBlockClassArguments; n++) {
        if (name == "Block" + std::to_string(n)) {
            cls->InstallPrimitive(new (GetHeap<HEAP_CLS>(), 0)
                                      VMEvaluationPrimitive(n),
                                  0, false);
        }
    }
}

/** No bytecode pushes a method. A #symbol is an object, see writeSymbols(). */
void FructusImage::ImageWriter::checkPushes(VMClass* cls, VMMethod* method) {
    for (size_t i = 0; i < method->bcLength;
         i += Bytecode::GetBytecodeLength(method->GetBytecode(i))) {
        uint8_t const bc = method->GetBytecode(i);
        if (bc == BC_PUSH_BLOCK) {
            checkPushes(cls, static_cast<VMMethod*>(method->GetConstant(i)));
        } else if (bc == BC_PUSH_CONSTANT) {
            vm_oop_t constant = method->GetConstant(i);
            if (IsInvokable(constant)) {
                Fail(Printable(cls->GetName()) + ">>#" +
                     Printable(method->GetSignature()) + " pushes " +
                     Describe(constant) + ", but the program can't have it");
            }
        }
    }
}

/**
 * The class side can get at the fields of the class, but the program can't
 * have the name, a selector, or the arrays of field names and invokables, and
 * only the VM changes them. `method` is `inv` or one of its blocks.
 */
void FructusImage::ImageWriter::checkClassFields(VMClass* metaclass,
                                                 VMInvokable* inv,
                                                 VMMethod* method) {
    auto check = [&](size_t index, bool write) {
        if (index >= VMClass::VMClassNumberOfFields ||
            (index == superClassField && !write)) {
            return;
        }
        std::string const field = classFields[index];
        Fail(Printable(metaclass->GetName()) + ">>#" +
             Printable(inv->GetSignature()) +
             (write ? " changes the class's " + field + ", which only the VM can"
                    : " reads the class's " + field +
                          ", which the program can't have"));
    };

    if (method == nullptr) {
        if (auto* getter = dynamic_cast<VMGetter*>(inv)) {
            check(getter->fieldIndex, false);
        } else if (auto* setter = dynamic_cast<VMSetter*>(inv)) {
            check(setter->fieldIndex, true);
        } else if (auto* m = dynamic_cast<VMMethod*>(inv)) {
            checkClassFields(metaclass, inv, m);
        }
        return;
    }

    for (size_t i = 0; i < method->bcLength;
         i += Bytecode::GetBytecodeLength(method->GetBytecode(i))) {
        uint8_t const bc = method->GetBytecode(i);
        // the field index, of the bytecodes that have one
        auto operand = [&]() { return method->GetBytecode(i + 1); };
        switch (bc) {
            case BC_PUSH_BLOCK:
                checkClassFields(metaclass, inv,
                                 static_cast<VMMethod*>(method->GetConstant(i)));
                break;
            case BC_PUSH_FIELD: check(operand(), false); break;
            case BC_PUSH_FIELD_0: check(0, false); break;
            case BC_PUSH_FIELD_1: check(1, false); break;
            case BC_RETURN_FIELD_0: check(0, false); break;
            case BC_RETURN_FIELD_1: check(1, false); break;
            case BC_RETURN_FIELD_2: check(2, false); break;
            case BC_POP_FIELD: check(operand(), true); break;
            case BC_POP_FIELD_0: check(0, true); break;
            case BC_POP_FIELD_1: check(1, true); break;
            case BC_INC_FIELD: check(operand(), true); break;
            case BC_INC_FIELD_PUSH: check(operand(), true); break;
            default: break;
        }
    }
}

void FructusImage::ImageWriter::checkClosedWorld() {
    for (VMClass* cls : classes) {
        installPrimitives(cls);
        for (VMClass* c : {cls, cls->GetClass()}) {
            classArrays.insert(c->GetInstanceInvokables());
            size_t const n = c->GetNumberOfInstanceInvokables();
            for (size_t i = 0; i < n; i++) {
                VMInvokable* inv = c->GetInstanceInvokable(i);
                classInvokables.emplace(
                    inv, std::make_pair(ClassLabel(c) + "." +
                                            Mangle(inv->GetSignature()),
                                        c));
                if (auto* method = dynamic_cast<VMMethod*>(inv)) {
                    checkPushes(cls, method);
                }
                // a message that isn't understood is an error of the VM,
                // which can't give the program the selector
                if (inv->GetSignature()->GetStdString() ==
                    "doesNotUnderstand:arguments:") {
                    ErrorPrintf(
                        "Warning: %s>>#doesNotUnderstand:arguments: is never "
                        "called on fructus, where a message that isn't "
                        "understood is an error\n",
                        Printable(c->GetName()).c_str());
                }
                if (c == cls->GetClass()) {
                    checkClassFields(c, inv, nullptr);
                }
            }
        }
    }
}

void FructusImage::ImageWriter::writeClass(VMClass* cls) {
    std::string const name = cls->GetName()->GetStdString();
    AsmFile file(*this, dir + "/" + name + ".s", name + ".som");
    file.SetOwner(cls);

    std::string const label = ClassLabel(cls);
    file.WriteGlobal(cls, label);
    file.WriteGlobal(cls->GetClass(), ClassLabel(cls->GetClass()));

    // the Universe::Global for the class
    std::string const nameRef = file.SelectorRef(cls->GetName());
    file.Out() << "\n\t.section\tsom_globals,\"a\",@progbits\n"
               << "\t.balign\t2\n"
               << "\t.short\t" << nameRef << ", " << label << "\n"
               << "\t.data\n";
    file.Finish();
}

void FructusImage::ImageWriter::writeUniverse(
    const std::vector<std::string>& arguments) {
    AsmFile file(*this, dir + "/_universe.s", "The universe of the program");
    auto* system = static_cast<VMObject*>(
        AS_OBJ(Universe::GetGlobal(SymbolFor("system"))));
    file.WriteGlobal(load_ptr(nilObject), "som_nil");
    file.WriteGlobal(load_ptr(trueObject), "som_true");
    file.WriteGlobal(load_ptr(falseObject), "som_false");
    file.WriteGlobal(system, "som_system");
    file.WriteGlobal(Universe::NewArrayFromStrings(arguments),
                     "som_arguments");

    // System>>initialize: on the host, but known now
    VMClass* mainClass = Universe::LoadClass(SymbolFor(arguments[0]));
    bool const hasRunWithArguments =
        mainClass->LookupInvokable(SymbolFor("run:")) != nullptr;
    std::ostream& out = file.Out();
    out << "\n\t.globl\tsom_main_class\n"
        << "\t.balign\t2\n"
        << "som_main_class:\n"
        << "\t.short\t" << ClassLabel(mainClass) << "\n"
        << "\n\t.globl\tsom_main_selector\n"
        << "som_main_selector:\n"
        << "\t.short\t"
        << file.SelectorRef(SymbolFor(hasRunWithArguments ? "run:" : "run"))
        << "\n";

    // indexed by the number of arguments, with the receiver
    out << "\n\t.globl\tsom_block_classes\n"
        << "som_block_classes:\n"
        << "\t.short\t0\t; no arguments, not even the receiver\n";
    for (int n = 1; n <= maxBlockClassArguments; n++) {
        VMSymbol* name = SymbolFor("Block" + std::to_string(n));
        vm_oop_t cls = Universe::GetGlobal(name);
        bool const inImage = cls != nullptr && IsClass(AS_OBJ(cls));
        out << "\t.short\t"
            << (inImage ? ClassLabel(static_cast<VMClass*>(AS_OBJ(cls)))
                        : std::string("0"))
            << "\n";
    }

    // the globals that aren't classes
    out << "\n\t.section\tsom_globals,\"a\",@progbits\n"
        << "\t.balign\t2\n";
    for (const char* name : {"nil", "true", "false", "system"}) {
        out << "\t.short\t" << file.SelectorRef(SymbolFor(name)) << ", som_"
            << name << "\n";
    }
    out << "\t.data\n";

    // the selectors the VM uses
#define FRUCTUS_REF_SYMBOL(mangled, selector) \
    file.SelectorRef(SymbolFor(selector));
    FRUCTUS_VM_SYMBOLS(FRUCTUS_REF_SYMBOL)
#undef FRUCTUS_REF_SYMBOL

    writeSymbols(file);
    file.Finish();
}

/**
 * The objects for the #symbols in the program, which the classes' files
 * referred to. A symbol is unique, so the program can compare them with ==.
 * It has nothing to do with the selector of the same name, and it isn't a
 * string, but has one with its characters: Symbol is a subclass of Object
 * with one field.
 */
void FructusImage::ImageWriter::writeSymbols(AsmFile& file) {
    if (symbolObjects.empty()) {
        return;
    }
    VMClass* symbol = load_ptr(symbolClass);
    if (symbol->GetNumberOfInstanceFields() != 1) {
        Fail("Symbol must have one field, the string of its characters");
    }
    for (auto const& [label, sym] : symbolObjects) {
        file.WriteSymbolObject(sym, label);
    }
}

void FructusImage::ImageWriter::Write(const std::vector<std::string>& arguments) {
    collectClasses(arguments);
    checkClosedWorld();
    for (VMClass* cls : classes) {
        writeClass(cls);
    }
    writeUniverse(arguments);
    writeSelectors();
}

/**
 * The number of each selector and name of a global the files refer to, as
 * an absolute ELF symbol: 2, 4, 6 and so on, see VMSymbol.h. The symbol
 * table is how tools/fructus-trace knows them.
 */
void FructusImage::ImageWriter::writeSelectors() {
    std::ofstream out(dir + "/_selectors.s");
    if (!out) {
        Fail("can't open " + dir + "/_selectors.s");
    }
    out << "; The selectors and the names of the globals, which are numbers\n"
        << "; Written for fructus by SOM++ -asm, see src/vm/FructusImage.h\n\n";
    size_t number = L::word;
    for (auto const& [label, sym] : selectors) {
        if (number >= L::selectorLimit) {
            Fail("the program has more than " +
                 std::to_string(L::selectorLimit / L::word - 1) +
                 " selectors and globals");
        }
        out << "\t.globl\t" << label << "\n"
            << "\t.set\t" << label << ", " << number << "\t; #"
            << Printable(sym) << "\n";
        number += L::word;
    }
}

void FructusImage::Write(const std::string& dir,
                         const std::vector<std::string>& arguments) {
    ImageWriter(dir).Write(arguments);
}
