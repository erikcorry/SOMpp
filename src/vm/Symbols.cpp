#include "Symbols.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../compiler/CompileAttempt.h"
#include "../memory/Heap.h"
#include "../misc/defs.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMSymbol.h"
#include "Print.h"
#include "Statistics.h"

/*
 * The symbol table: open addressing with linear probing. The hash is
 * computed from the characters, not the address, so a moving GC does not
 * need to rehash.
 */
static GCSymbol** symbolTable = nullptr;
static size_t symbolTableSize = 0;  // a power of two
static size_t symbolCount = 0;

GCSymbol* symbolSelf;
GCSymbol* symbolSuper;
GCSymbol* symbolBlockSelf;
GCSymbol* symbolIfTrue;
GCSymbol* symbolIfFalse;
GCSymbol* symbolPlus;
GCSymbol* symbolMinus;

static size_t hashChars(const char* str, size_t length) {
    size_t hash = 5381U;
    for (size_t i = 0; i < length; i++) {
        hash = ((hash << 5U) + hash) + (unsigned char)str[i];
    }
    return hash;
}

static size_t findSlot(GCSymbol** table, size_t size, const char* str,
                       size_t length) {
    size_t const mask = size - 1;
    size_t i = hashChars(str, length) & mask;
    while (table[i] != nullptr) {
        VMSymbol const* sym = load_ptr(table[i]);
        if (sym->GetStringLength() == length &&
            memcmp(sym->GetRawChars(), str, length) == 0) {
            return i;
        }
        i = (i + 1) & mask;
    }
    return i;
}

static void growSymbolTable() {
    size_t const newSize = symbolTableSize == 0 ? 256 : symbolTableSize * 2;
    auto** newTable = (GCSymbol**)calloc(newSize, sizeof(GCSymbol*));
    if (newTable == nullptr) {
        ErrorExit("out of memory for the symbol table");
    }
    for (size_t i = 0; i < symbolTableSize; i++) {
        if (symbolTable[i] != nullptr) {
            VMSymbol const* sym = load_ptr(symbolTable[i]);
            newTable[findSlot(newTable, newSize, sym->GetRawChars(),
                              sym->GetStringLength())] = symbolTable[i];
        }
    }
    free(symbolTable);
    symbolTable = newTable;
    symbolTableSize = newSize;
}

VMSymbol* NewSymbol(const size_t length, const char* str) {
    if ((symbolCount + 1) * 4 > symbolTableSize * 3) {
        growSymbolTable();
    }
    auto* result =
        new (GetHeap<HEAP_CLS>(), PADDED_SIZE(length)) VMSymbol(length, str);
    if (result == nullptr) {
        AbandonIfCompiling();
        return nullptr;
    }
    symbolTable[findSlot(symbolTable, symbolTableSize, str, length)] =
        store_root(result);
    symbolCount++;

    recordStat(Allocation, "VMSymbol", result->GetObjectSize());
    return result;
}

VMSymbol* SymbolFor(const char* str, size_t length) {
    if (symbolTableSize != 0) {
        GCSymbol* const found =
            symbolTable[findSlot(symbolTable, symbolTableSize, str, length)];
        if (found != nullptr) {
            return load_ptr(found);
        }
    }
    return NewSymbol(length, str);
}

VMSymbol* SymbolFor(const char* str) {
    return SymbolFor(str, strlen(str));
}

void InitializeSymbols() {
    symbolSelf = store_root(SymbolFor("self"));
    symbolSuper = store_root(SymbolFor("super"));
    symbolBlockSelf = store_root(SymbolFor("$blockSelf"));
    symbolIfTrue = store_root(SymbolFor("ifTrue:"));
    symbolIfFalse = store_root(SymbolFor("ifFalse:"));

    symbolPlus = store_root(SymbolFor("+"));
    symbolMinus = store_root(SymbolFor("-"));
}

void WalkSymbols(walk_heap_fn walk) {
    for (size_t i = 0; i < symbolTableSize; i++) {
        if (symbolTable[i] != nullptr) {
            symbolTable[i] = static_cast<GCSymbol*>(walk(symbolTable[i]));
        }
    }

    // reassign symbols
    symbolSelf = static_cast<GCSymbol*>(walk(symbolSelf));
    symbolSuper = static_cast<GCSymbol*>(walk(symbolSuper));
    symbolBlockSelf = static_cast<GCSymbol*>(walk(symbolBlockSelf));
    symbolIfTrue = static_cast<GCSymbol*>(walk(symbolIfTrue));
    symbolIfFalse = static_cast<GCSymbol*>(walk(symbolIfFalse));

    symbolPlus = static_cast<GCSymbol*>(walk(symbolPlus));
    symbolMinus = static_cast<GCSymbol*>(walk(symbolMinus));
}
