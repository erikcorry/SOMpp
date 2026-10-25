#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef FRUCTUS
  #include <string>
#endif

void Print(const char* str);
/** Print a string that is not null-terminated, e.g. a VMString's chars. */
void PrintChars(const char* str, size_t length);
void ErrorPrint(const char* str);
/** fructus' printf doesn't do %.*s, so this prints a VMString's chars. */
void ErrorPrintChars(const char* str, size_t length);
void ErrorPrintf(const char* format, ...)
    __attribute__((format(printf, 1, 2)));

__attribute__((noreturn)) __attribute__((noinline)) void Quit(int32_t err);
__attribute__((noreturn)) __attribute__((noinline, cold)) void ErrorExit(
    const char* /*err*/);

#ifdef FRUCTUS
/**
 * `prefix`@`value` in hex, like sel@12 or class@c4f2, which is how fructus
 * names selectors, classes and pcs, since it has no names: tools/fructus-trace
 * translates them. `out` has room for the prefix, 6 chars and a 0.
 */
void FormatTagged(char* out, const char* prefix, uintptr_t value);
#endif

#ifndef FRUCTUS
inline void Print(const std::string& str) {
    PrintChars(str.data(), str.length());
}
inline void ErrorPrint(const std::string& str) {
    ErrorPrint(str.c_str());
}
__attribute__((noreturn)) inline void ErrorExit(const std::string& err) {
    ErrorExit(err.c_str());
}
#endif
