#pragma once

/*
 * The primitives of fructus, which are in the image as objects that refer to
 * their routines by name, see vm/FructusImage.h. Nothing is installed at run
 * time: the host writes each class's methods with these in place, replacing
 * whatever the class library has for the selector, and the linker resolves
 * the routines. This is the closed world, so a primitive is assumed to do
 * what the method it replaces does, and an unused routine is dropped by
 * --gc-sections.
 *
 * The program can't make or get symbols or methods, so there are no
 * primitives that take or answer them. Classes have no names, so there is
 * no Class>>name, and Class>>asString answers class@<address>.
 *
 * P(class, classSide, selector, kind, routine), where kind is the routine's
 * type: Frame, Unary, Binary, or Ternary.
 */

#include "../primitivesCore/Primitives.h"

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define FRUCTUS_PRIMITIVES(P)                                          \
    P(Array, true, "new:", Binary, arrNew)                             \
    P(Array, false, "at:", Binary, arrAt)                              \
    P(Array, false, "at:put:", Ternary, arrAtPut)                      \
    P(Array, false, "length", Unary, arrLength)                        \
    P(Array, false, "copy", Unary, arrCopy)                            \
                                                                       \
    P(Block, false, "restart", Frame, bRestart)                        \
                                                                       \
    P(Class, false, "new", Unary, clsNew)                              \
    P(Class, false, "asString", Unary, clsAsString)                    \
    P(Class, false, "superclass", Unary, clsSuperclass)                \
                                                                       \
    P(Integer, false, "+", Binary, intPlus)                            \
    P(Integer, false, "-", Binary, intMinus)                           \
    P(Integer, false, "*", Binary, intStar)                            \
    P(Integer, false, "rem:", Binary, intRem)                          \
    P(Integer, false, "bitXor:", Binary, intBitwiseXor)                \
    P(Integer, false, "<<", Binary, intLeftShift)                      \
    P(Integer, false, ">>>", Binary, intUnsignedRightShift)            \
    P(Integer, false, "/", Binary, intSlash)                           \
    P(Integer, false, "%", Binary, intPercent)                         \
    P(Integer, false, "&", Binary, intAnd)                             \
    P(Integer, false, "=", Binary, intEqual)                           \
    P(Integer, false, "==", Binary, intEqual)                          \
    P(Integer, false, "<", Binary, intLowerthan)                       \
    P(Integer, false, "asString", Unary, intAsString)                  \
    P(Integer, false, "atRandom", Unary, intAtRandom)                  \
    P(Integer, true, "fromString:", Binary, intFromString)             \
    P(Integer, false, "<=", Binary, intLowerThanEqual)                 \
    P(Integer, false, ">", Binary, intGreaterThan)                     \
    P(Integer, false, ">=", Binary, intGreaterThanEqual)               \
    P(Integer, false, "<>", Binary, intUnequal)                        \
    P(Integer, false, "~=", Binary, intUnequal)                        \
    P(Integer, false, "abs", Unary, intAbs)                            \
    P(Integer, false, "min:", Binary, intMin)                          \
    P(Integer, false, "max:", Binary, intMax)                          \
    P(Integer, false, "to:", Binary, intRange)                         \
                                                                       \
    P(Object, false, "==", Binary, objEqualequal)                      \
    P(Object, false, "objectSize", Unary, objObjectSize)               \
    P(Object, false, "hashcode", Unary, objHashcode)                   \
    P(Object, false, "inspect", Unary, objInspect)                     \
    P(Object, false, "halt", Unary, objHalt)                           \
    P(Object, false, "class", Unary, objClass)                         \
                                                                       \
    P(String, false, "concatenate:", Binary, strConcatenate_)          \
    P(String, false, "hashcode", Unary, strHashcode)                   \
    P(String, false, "length", Unary, strLength)                       \
    P(String, false, "=", Binary, strEqual)                            \
    P(String, false, "primSubstringFrom:to:", Ternary,                 \
      strPrimSubstringFromTo)                                          \
    P(String, false, "isWhiteSpace", Unary, strIsWhiteSpace)           \
    P(String, false, "isLetters", Unary, strIsLetters)                 \
    P(String, false, "isDigits", Unary, strIsDigits)                   \
    P(String, false, "charAt:", Binary, strCharAt)                     \
                                                                       \
    P(System, false, "exit:", Binary, sysExit_)                        \
    P(System, false, "printString:", Binary, sysPrintString_)          \
    P(System, false, "printNewline", Unary, sysPrintNewline)           \
    P(System, false, "errorPrint:", Binary, sysErrorPrint_)            \
    P(System, false, "errorPrintln:", Binary, sysErrorPrintNewline_)   \
    P(System, false, "time", Unary, sysTime)                           \
    P(System, false, "ticks", Unary, sysTicks)                         \
    P(System, false, "fullGC", Unary, sysFullGC)                       \
    P(System, false, "printStackTrace", Frame, sysPrintStackTrace)

/**
 * The routines have C linkage, so the image can refer to them by name. The
 * definitions must have the declared kind's type, or they don't compile.
 */
#define FRUCTUS_DECLARE_PRIMITIVE(cls, classSide, selector, kind, routine) \
    extern "C" kind##PrimitiveRoutine routine;
FRUCTUS_PRIMITIVES(FRUCTUS_DECLARE_PRIMITIVE)
#undef FRUCTUS_DECLARE_PRIMITIVE
// NOLINTEND(cppcoreguidelines-macro-usage)
