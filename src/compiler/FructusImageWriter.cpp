/*
 * Writes the class library for fructus as assembler, see vm/FructusImage.h.
 *
 * Runs on the host, after the classes were compiled, and writes each object
 * field by field at the offsets in FructusLayout, with references between
 * objects as symbolic address constants for the linker.
 */

#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../interpreter/bytecodes.h"
#include "../primitives/FructusPrimitives.h"
#include "../vm/FructusImage.h"
#include "../vm/Globals.h"
#include "../vm/Print.h"
#include "../vm/Symbols.h"
#include "../vm/Universe.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMArray.h"
#include "../vmobjects/VMClass.h"
#include "../vmobjects/VMEvaluationPrimitive.h"
#include "../vmobjects/VMMethod.h"
#include "../vmobjects/VMObject.h"
#include "../vmobjects/VMPrimitive.h"
#include "../vmobjects/VMString.h"
#include "../vmobjects/VMSymbol.h"
#include "../vmobjects/VMTrivialMethod.h"

namespace L = FructusLayout;

namespace {

/** Classes fructus has no use for, which are left out of the image. */
const char* const unsupportedClasses[] = {"Double"};

/** Classes the VM itself looks up, besides the system classes. */
const char* const vmClasses[] = {"Block",  "Block1", "Block2", "Block3",
                                 "Block4", "True",   "False",  "System"};

/**
 * Fructus is a closed world: every send's selector is in the program, which
 * perform would break.
 */
const char* const dynamicSendSelectors[] = {
    "perform:", "perform:withArguments:", "perform:inSuperclass:",
    "perform:withArguments:inSuperclass:"};

/** The highest number of arguments, with the receiver, of a block class. */
constexpr int maxBlockClassArguments = 4;

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
 * not classes or symbols are local to the file that first refers to them,
 * and symbols are written by every file that refers to them.
 */
class FructusImage::AsmFile {
public:
    AsmFile(ImageWriter& image, const std::string& path,
            const std::string& title);

    /** Write an object with a global label. */
    void WriteGlobal(AbstractVMObject* obj, const std::string& label);

    /** Write the local objects and symbols that were referred to. */
    void Finish();

    /** The assembler expression for a reference to `obj`. */
    std::string Ref(vm_oop_t obj);

    std::ostream& Out() { return out; }

private:
    void write(AbstractVMObject* obj, const std::string& label);
    void writeHeader(ObjectWriter& w, const std::string& vtable,
                     const std::string& comment);
    void writeVMObject(VMObject* obj, const std::string& label);
    void writeString(VMString* str, const std::string& label);
    void writeSymbol(VMSymbol* sym, const std::string& label);
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
    void writeBytecodes(ObjectWriter& w, size_t offset, VMMethod* method);
    std::string describe(VMInvokable* inv);

    ImageWriter& image;
    std::ofstream out;
    std::map<AbstractVMObject*, std::string> localLabels;
    std::deque<AbstractVMObject*> pending;
    std::map<std::string, VMSymbol*> symbols;  // by label, for stable output
};

class FructusImage::ImageWriter {
public:
    explicit ImageWriter(std::string dir) : dir(std::move(dir)) {}

    void Write(const std::vector<std::string>& arguments);

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

    /** The fructus primitive `prim` stands for, or nullptr if there's none. */
    const PrimitiveSpec* PrimitiveFor(VMPrimitive* prim) const {
        auto const it = primitives.find(prim);
        return it == primitives.end() ? nullptr : it->second;
    }

private:
    void collectClasses(const std::vector<std::string>& arguments);
    void checkClosedWorld();
    void checkSends(VMClass* cls, VMMethod* method);
    void installPrimitives(VMClass* cls);
    void removeDynamicSends(VMClass* cls);
    void addClass(const char* name, bool required);
    void addGlobalsOf(VMClass* cls);
    void addGlobalsOf(VMInvokable* inv);
    void writeClass(VMClass* cls);
    void writeUniverse(const std::vector<std::string>& arguments);

    std::string dir;
    std::vector<VMClass*> classes;
    std::deque<VMClass*> toScan;
    std::map<AbstractVMObject*, std::string> classLabels;
    std::map<VMPrimitive*, const PrimitiveSpec*> primitives;
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

std::string FructusImage::AsmFile::Ref(vm_oop_t obj) {
    if (IS_TAGGED(obj)) {
        auto const value = (int64_t)SMALL_INT_VAL(obj);
        if (value < L::smallIntMin || value > L::smallIntMax) {
            Fail("the integer " + std::to_string(value) +
                 " is too big for fructus");
        }
        return std::to_string(value * 2 + 1);
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
        std::string label = SymbolLabel(sym);
        symbols[label] = sym;
        return label;
    }

    auto const it = localLabels.find(o);
    if (it != localLabels.end()) {
        return it->second;
    }
    std::string label = ".Lo" + std::to_string(localLabels.size());
    localLabels[o] = label;
    pending.push_back(o);
    return label;
}

void FructusImage::AsmFile::WriteGlobal(AbstractVMObject* obj, const std::string& label) {
    out << "\n\t.globl\t" << label << "\n";
    write(obj, label);
}

void FructusImage::AsmFile::Finish() {
    while (!pending.empty()) {
        AbstractVMObject* obj = pending.front();
        pending.pop_front();
        out << "\n";
        write(obj, localLabels[obj]);
    }

    // each symbol in a COMDAT group of its own, with its entry in the table
    // of all symbols
    for (auto& [label, sym] : symbols) {
        out << "\n\t.section\t.data." << label << ",\"awG\",@progbits,"
            << label << ",comdat\n"
            << "\t.globl\t" << label << "\n";
        writeSymbol(sym, label);
        out << "\t.section\tsom_symbols,\"aG\",@progbits," << label
            << ",comdat\n"
            << "\t.balign\t2\n"
            << "\t.short\t" << label << "\n";
    }
}

void FructusImage::AsmFile::write(AbstractVMObject* obj, const std::string& label) {
    out << "\t.balign\t2\n" << label << ":\n";

    // the most derived classes first
    if (auto* sym = dynamic_cast<VMSymbol*>(obj)) {
        writeSymbol(sym, label);
    } else if (auto* str = dynamic_cast<VMString*>(obj)) {
        writeString(str, label);
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
    w.Word(L::gcField, 0, "gcField");
}

/** VMObject, VMClass, VMArray: a header, and fields that are all oops. */
void FructusImage::AsmFile::writeVMObject(VMObject* obj, const std::string& label) {
    ObjectWriter w(out, label);
    size_t const numberOfFields = obj->GetNumberOfFields();
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
    w.Word(L::VMObject_hash, w.Self(), "hash");
    w.Word(L::VMObject_totalObjectSize, (int64_t)size, "totalObjectSize");
    w.Word(L::VMObject_numberOfFields, (int64_t)numberOfFields,
           "numberOfFields");
    w.Word(L::VMObject_clazz, Ref(obj->GetClass()), "class");

    static const char* const classFields[] = {"name", "instanceFields",
                                              "instanceInvokables",
                                              "superClass"};
    bool const isClass = dynamic_cast<VMClass*>(obj) != nullptr;
    for (size_t i = 0; i < numberOfFields; i++) {
        std::string const fieldComment =
            isClass && i < 4 ? classFields[i] : "field " + std::to_string(i);
        w.Word(L::VMObject_size + i * L::word, Ref(obj->GetField(i)),
               fieldComment);
    }
    w.End(size);
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

void FructusImage::AsmFile::writeSymbol(VMSymbol* sym, const std::string& label) {
    out << "\t.balign\t2\n" << label << ":\n";
    ObjectWriter w(out, label);
    size_t const length = sym->GetStringLength();
    writeHeader(w, Vtable("8VMSymbol"), "Symbol #" + Printable(sym));
    w.Word(L::VMString_length, (int64_t)length, "length");
    w.Word(L::VMString_chars, w.Self(L::VMSymbol_size), "chars");
    w.Byte(L::VMSymbol_numberOfArgumentsOfSignature,
           sym->numberOfArgumentsOfSignature, "numberOfArgumentsOfSignature");
    w.Raw(L::VMSymbol_cachedClass_invokable, 3 * L::word,
          "\t.short\t0, 0, 0\t; cachedClass_invokable\n");
    w.Word(L::VMSymbol_nextCachePos, 0, "nextCachePos");
    w.Raw(L::VMSymbol_cachedInvokable, 3 * L::word,
          "\t.short\t0, 0, 0\t; cachedInvokable\n");
    w.Raw(L::VMSymbol_size, length,
          "\t.ascii\t" + Quote(sym->GetRawChars(), length) + "\n");
    w.End(L::VMSymbol_size + L::PaddedSize(length));
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
    w.Word(L::VMInvokable_hash, w.Self(), "hash");
    w.Word(L::VMInvokable_signature, Ref(inv->GetSignature()), "signature");
    w.Word(L::VMInvokable_holder, Ref(inv->GetHolder()), "holder");
}

void FructusImage::AsmFile::writeMethod(VMMethod* method, const std::string& label) {
    ObjectWriter w(out, label);
    size_t const numberOfConstants = method->numberOfConstants;
    size_t const bcLength = method->bcLength;
    size_t const bytecodes = L::VMMethod_size + numberOfConstants * L::word;

    writeInvokableHeader(w, method, Vtable("8VMMethod"), "Method");
    w.Word(L::VMMethod_numberOfLocals, (int64_t)method->numberOfLocals,
           "numberOfLocals");
    w.Word(L::VMMethod_maximumNumberOfStackElements,
           (int64_t)method->maximumNumberOfStackElements,
           "maximumNumberOfStackElements");
    w.Word(L::VMMethod_bcLength, (int64_t)bcLength, "bcLength");
    w.Byte(L::VMMethod_numberOfArguments, method->numberOfArguments,
           "numberOfArguments");
    w.Word(L::VMMethod_numberOfConstants, (int64_t)numberOfConstants,
           "numberOfConstants");
    w.Word(L::VMMethod_indexableFields, w.Self(L::VMMethod_size),
           "indexableFields");
    w.Word(L::VMMethod_bytecodes, w.Self(bytecodes), "bytecodes");

    for (size_t i = 0; i < numberOfConstants; i++) {
        w.Word(L::VMMethod_size + i * L::word,
               Ref(load_ptr(method->indexableFields[i])),
               "constant " + std::to_string(i));
    }
    writeBytecodes(w, bytecodes, method);
    w.End(L::VMMethod_size +
          L::PaddedSize(bcLength + numberOfConstants * L::word));
}

void FructusImage::AsmFile::writeBytecodes(ObjectWriter& w, size_t offset,
                             VMMethod* method) {
    size_t const bcLength = method->bcLength;
    size_t i = 0;
    while (i < bcLength) {
        uint8_t const bc = method->bytecodes[i];
        size_t const length = Bytecode::GetBytecodeLength(bc);
        if (length == 0 || i + length > bcLength) {
            Fail("bad bytecode " + std::to_string(bc) + " in " +
                 describe(method));
        }
        std::ostringstream line;
        line << "\t.byte\t";
        for (size_t j = 0; j < length; j++) {
            line << (j == 0 ? "" : ", ") << (unsigned)method->bytecodes[i + j];
        }
        std::string name = Bytecode::GetBytecodeName(bc);
        name.erase(name.find_last_not_of(' ') + 1);
        line << "\t; " << name << "\n";
        w.Raw(offset + i, length, line.str());
        i += length;
    }
}

void FructusImage::AsmFile::writePrimitive(VMPrimitive* prim,
                                           const std::string& label) {
    ObjectWriter w(out, label);
    if (const PrimitiveSpec* spec = image.PrimitiveFor(prim)) {
        writeInvokableHeader(w, prim, Vtable(PrimitiveClass(spec->kind)),
                             std::string(spec->kind) + " primitive");
        w.Word(L::VMPrimitive_prim, spec->routine, "routine");
    } else {
        // a primitive fructus doesn't have, which fails when it's called
        if (!prim->IsEmpty()) {
            Fail("the primitive " + describe(prim) +
                 " is bound on the host, and fructus doesn't have it");
        }
        writeInvokableHeader(w, prim, Vtable("11VMPrimitive"),
                             "Missing primitive");
        w.Word(L::VMPrimitive_prim, "som_empty_primitive", "routine");
    }
    w.End(L::VMPrimitive_size);
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
    w.Word(L::VMGlobalReturn_globalName, Ref(load_ptr(inv->globalName)),
           "globalName");
    w.Byte(L::VMGlobalReturn_numberOfArguments, inv->numberOfArguments,
           "numberOfArguments");
    w.End(L::VMGlobalReturn_size);
}

void FructusImage::AsmFile::writeGetter(VMGetter* inv, const std::string& label) {
    ObjectWriter w(out, label);
    writeInvokableHeader(w, inv, Vtable("8VMGetter"), "Getter");
    w.Word(L::VMGetter_fieldIndex, (int64_t)inv->fieldIndex, "fieldIndex");
    w.Byte(L::VMGetter_numberOfArguments, inv->numberOfArguments,
           "numberOfArguments");
    w.End(L::VMGetter_size);
}

void FructusImage::AsmFile::writeSetter(VMSetter* inv, const std::string& label) {
    ObjectWriter w(out, label);
    writeInvokableHeader(w, inv, Vtable("8VMSetter"), "Setter");
    w.Word(L::VMSetter_fieldIndex, (int64_t)inv->fieldIndex, "fieldIndex");
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
         i += Bytecode::GetBytecodeLength(method->bytecodes[i])) {
        if (method->bytecodes[i] == BC_PUSH_GLOBAL) {
            auto* name = static_cast<VMSymbol*>(
                method->GetConstantWithIndex(method->bytecodes[i + 1]));
            addClass(name->GetStdString().c_str(), false);
        }
    }
    // blocks
    for (size_t i = 0; i < method->numberOfConstants; i++) {
        vm_oop_t constant = load_ptr(method->indexableFields[i]);
        if (!IS_TAGGED(constant)) {
            if (auto* block = dynamic_cast<VMMethod*>(AS_OBJ(constant))) {
                addGlobalsOf(block);
            }
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

/** Leave out the methods that send a selector the program doesn't have. */
void FructusImage::ImageWriter::removeDynamicSends(VMClass* cls) {
    VMArray* invokables = cls->GetInstanceInvokables();
    size_t const n = invokables->GetNumberOfIndexableFields();
    std::vector<vm_oop_t> kept;
    for (size_t i = 0; i < n; i++) {
        auto* inv = static_cast<VMInvokable*>(invokables->GetIndexableField(i));
        std::string const selector = inv->GetSignature()->GetStdString();
        bool dynamic = false;
        for (const char* d : dynamicSendSelectors) {
            dynamic = dynamic || selector == d;
        }
        if (!dynamic) {
            kept.push_back(inv);
        }
    }
    if (kept.size() == n) {
        return;
    }
    VMArray* result = Universe::NewArray(kept.size());
    for (size_t i = 0; i < kept.size(); i++) {
        result->SetIndexableField(i, kept[i]);
    }
    cls->SetInstanceInvokables(result);
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
    // so on
    for (int n = 1; n <= maxBlockClassArguments; n++) {
        if (name == "Block" + std::to_string(n)) {
            cls->InstallPrimitive(new (GetHeap<HEAP_CLS>(), 0)
                                      VMEvaluationPrimitive(n),
                                  0, false);
        }
    }
}

void FructusImage::ImageWriter::checkSends(VMClass* cls, VMMethod* method) {
    for (size_t i = 0; i < method->bcLength;
         i += Bytecode::GetBytecodeLength(method->bytecodes[i])) {
        uint8_t const bc = method->bytecodes[i];
        if (bc != BC_SEND && bc != BC_SEND_1 && bc != BC_SUPER_SEND) {
            continue;
        }
        std::string const selector =
            static_cast<VMSymbol*>(
                method->GetConstantWithIndex(method->bytecodes[i + 1]))
                ->GetStdString();
        for (const char* d : dynamicSendSelectors) {
            if (selector == d) {
                Fail(Printable(cls->GetName()) + " sends #" + selector +
                     ", but fructus is a closed world");
            }
        }
    }
    for (size_t i = 0; i < method->numberOfConstants; i++) {
        vm_oop_t constant = load_ptr(method->indexableFields[i]);
        if (!IS_TAGGED(constant)) {
            if (auto* block = dynamic_cast<VMMethod*>(AS_OBJ(constant))) {
                checkSends(cls, block);
            }
        }
    }
}

/** Fructus has no perform, and its primitives are known when it's linked. */
void FructusImage::ImageWriter::checkClosedWorld() {
    for (VMClass* cls : classes) {
        for (VMClass* c : {cls, cls->GetClass()}) {
            removeDynamicSends(c);
            size_t const n = c->GetNumberOfInstanceInvokables();
            for (size_t i = 0; i < n; i++) {
                if (auto* method =
                        dynamic_cast<VMMethod*>(c->GetInstanceInvokable(i))) {
                    checkSends(cls, method);
                }
            }
        }
        installPrimitives(cls);
    }
}

void FructusImage::ImageWriter::writeClass(VMClass* cls) {
    std::string const name = cls->GetName()->GetStdString();
    AsmFile file(*this, dir + "/" + name + ".s", name + ".som");

    std::string const label = ClassLabel(cls);
    file.WriteGlobal(cls, label);
    file.WriteGlobal(cls->GetClass(), ClassLabel(cls->GetClass()));

    // the Universe::Global for the class
    std::string const nameRef = file.Ref(cls->GetName());
    file.Out() << "\n\t.section\tsom_globals,\"a\",@progbits\n"
               << "\t.balign\t2\n"
               << "\t.short\t" << nameRef << ", " << label << "\n"
               << "\t.data\n";
    file.Finish();
}

void FructusImage::ImageWriter::writeUniverse(const std::vector<std::string>& arguments) {
    AsmFile file(*this, dir + "/_universe.s",
                 "nil, true, false, and the program's arguments");
    file.WriteGlobal(load_ptr(nilObject), "som_nil");
    file.WriteGlobal(load_ptr(trueObject), "som_true");
    file.WriteGlobal(load_ptr(falseObject), "som_false");
    file.WriteGlobal(Universe::NewArrayFromStrings(arguments),
                     "som_arguments");
    file.Finish();
}

void FructusImage::ImageWriter::Write(const std::vector<std::string>& arguments) {
    collectClasses(arguments);
    checkClosedWorld();
    for (VMClass* cls : classes) {
        writeClass(cls);
    }
    writeUniverse(arguments);
}

void FructusImage::Write(const std::string& dir,
                         const std::vector<std::string>& arguments) {
    ImageWriter(dir).Write(arguments);
}
