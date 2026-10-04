#include "Print.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../misc/defs.h"
#include "Universe.h"

void Print(const char* str) {
    fputs(str, stdout);
}

void PrintChars(const char* str, size_t length) {
    fwrite(str, 1, length, stdout);
}

void ErrorPrint(const char* str) {
    fputs(str, stderr);
}

void ErrorPrintChars(const char* str, size_t length) {
    fwrite(str, 1, length, stderr);
}

void ErrorPrintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

__attribute__((noreturn)) __attribute__((noinline)) void ErrorExit(
    const char* err) {
    ErrorPrintf("Runtime error: %s\n", err);
    Quit(ERR_FAIL);
}

__attribute__((noreturn)) __attribute__((noinline)) void Quit(int32_t err) {
    Universe::Shutdown();
    exit(err);
}

#ifdef FRUCTUS
vm_oop_t IntegerOverflow() {
    ErrorExit("Integer overflow");
}
#endif
