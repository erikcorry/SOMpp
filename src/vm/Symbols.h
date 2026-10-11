#pragma once

/*
 * The symbol table. Fructus has none: its symbols are all in the image,
 * where the linker makes them unique, and none are made at run time. See
 * FructusImage.h.
 */

#ifndef FRUCTUS

#include <string.h>

#include "../vmobjects/VMSymbol.h"

VMSymbol* SymbolFor(const char* str, size_t length);
VMSymbol* SymbolFor(const char* str);
inline VMSymbol* SymbolFor(const std::string& str) {
    return SymbolFor(str.data(), str.length());
}

void InitializeSymbols();

void WalkSymbols(walk_heap_fn walk);

#ifdef UNITTESTS
VMSymbol* NewSymbol(size_t length, const char* str);
inline VMSymbol* NewSymbol(const char* str) {
    return NewSymbol(strlen(str), str);
}
#endif

extern GCSymbol* symbolSelf;
extern GCSymbol* symbolSuper;
extern GCSymbol* symbolBlockSelf;

extern GCSymbol* symbolIfTrue;
extern GCSymbol* symbolIfFalse;

extern GCSymbol* symbolPlus;
extern GCSymbol* symbolMinus;
extern GCSymbol* symbolEqualEqual;

const char* const strBlockSelf = "$blockSelf";
const char* const strSuper = "super";
const char* const strSelf = "self";

#endif
