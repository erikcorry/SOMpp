#pragma once

#include "../vmobjects/VMSymbol.h"

VMSymbol* SymbolFor(const char* str, size_t length);
VMSymbol* SymbolFor(const char* str);

void InitializeSymbols();

void WalkSymbols(walk_heap_fn walk);

#ifdef UNITTESTS
VMSymbol* NewSymbol(size_t length, const char* str);
#endif

extern GCSymbol* symbolSelf;
extern GCSymbol* symbolSuper;
extern GCSymbol* symbolBlockSelf;

extern GCSymbol* symbolIfTrue;
extern GCSymbol* symbolIfFalse;

extern GCSymbol* symbolPlus;
extern GCSymbol* symbolMinus;

const char* const strBlockSelf = "$blockSelf";
const char* const strSuper = "super";
const char* const strSelf = "self";
