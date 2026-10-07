/*
 *
 *
 Copyright (c) 2007 Michael Haupt, Tobias Pape, Arne Bergmann
 Software Architecture Group, Hasso Plattner Institute, Potsdam, Germany
 http://www.hpi.uni-potsdam.de/swa/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#include "System.h"

#include "FructusPrimitives.h"

#include <stdint.h>

#include "../compiler/CompileAttempt.h"
#include "../memory/Heap.h"
#include "../misc/defs.h"
#include "../vm/Globals.h"
#include "../vm/Print.h"
#include "../vm/Universe.h"
#include "../vmobjects/ObjectFormats.h"
#ifndef FRUCTUS
#include "../vmobjects/VMBigInteger.h"  // NOLINT(misc-include-cleaner)
#endif
#include "../vmobjects/VMClass.h"
#include "../vmobjects/VMFrame.h"
#include "../vmobjects/VMString.h"
#include "../vmobjects/VMSymbol.h"

#ifndef FRUCTUS
  #include <sys/time.h>
#endif

#ifndef FRUCTUS
// fructus has no symbols at run time, and all its globals are in the image
static vm_oop_t sysGlobal_(vm_oop_t /*unused*/, vm_oop_t rightObj) {
    auto* arg = static_cast<VMSymbol*>(rightObj);
    vm_oop_t result = Universe::GetGlobal(arg);

    return (result != nullptr) ? result : load_ptr(nilObject);
}

static vm_oop_t sysGlobalPut(vm_oop_t sys, vm_oop_t sym, vm_oop_t val) {
    auto* arg = static_cast<VMSymbol*>(sym);
    Universe::SetGlobal(arg, val);
    return sys;
}

static vm_oop_t sysHasGlobal_(vm_oop_t /*unused*/, vm_oop_t rightObj) {
    auto* arg = static_cast<VMSymbol*>(rightObj);

    if (Universe::HasGlobal(arg)) {
        return load_ptr(trueObject);
    }
    return load_ptr(falseObject);
}

static vm_oop_t sysLoad_(vm_oop_t /*unused*/, vm_oop_t rightObj) {
    auto* arg = static_cast<VMSymbol*>(rightObj);
    // answers nullptr, for a retry, if an allocation failed
    return CompileAttempt::Run([arg]() { return Universe::LoadClass(arg); });
}
#endif

vm_oop_t sysExit_(vm_oop_t /*unused*/, vm_oop_t err) {
    som_int_t const err_no = SMALL_INT_VAL(err);
    Quit((int32_t)err_no);
}

vm_oop_t sysPrintString_(vm_oop_t leftObj, vm_oop_t rightObj) {
    auto* arg = static_cast<VMString*>(rightObj);
    PrintChars(arg->GetRawChars(), arg->GetStringLength());
    return leftObj;
}

vm_oop_t sysPrintNewline(vm_oop_t leftObj) {
    Print("\n");
    return leftObj;
}

vm_oop_t sysErrorPrint_(vm_oop_t leftObj, vm_oop_t rightObj) {
    auto* arg = static_cast<VMString*>(rightObj);
    ErrorPrintChars(arg->GetRawChars(), arg->GetStringLength());
    return leftObj;
}

vm_oop_t sysErrorPrintNewline_(vm_oop_t leftObj, vm_oop_t rightObj) {
    auto* arg = static_cast<VMString*>(rightObj);
    ErrorPrintChars(arg->GetRawChars(), arg->GetStringLength());
    ErrorPrint("\n");
    return leftObj;
}

#ifdef FRUCTUS
// there is no clock yet
vm_oop_t sysTime(vm_oop_t /*unused*/) {
    return NEW_INT(0);
}

vm_oop_t sysTicks(vm_oop_t /*unused*/) {
    return NEW_INT(0);
}
#else
static struct timeval start_time;

vm_oop_t sysTime(vm_oop_t /*unused*/) {
    struct timeval now{};

    gettimeofday(&now, nullptr);

    int64_t const diff =
        ((now.tv_sec - start_time.tv_sec) * 1000) +   // seconds
        ((now.tv_usec - start_time.tv_usec) / 1000);  // useconds

    return NEW_INT(diff);
}

vm_oop_t sysTicks(vm_oop_t /*unused*/) {
    struct timeval now{};

    gettimeofday(&now, nullptr);

    int64_t const diff =
        ((now.tv_sec - start_time.tv_sec) * 1000 * 1000) +  // seconds
        ((now.tv_usec - start_time.tv_usec));               // useconds

    return NEW_INT(diff);
}
#endif

vm_oop_t sysFullGC(vm_oop_t /*unused*/) {
    // not safe to do it immediatly, will be done when it is ok, i.e., in the
    // interpreter loop
    GetHeap<HEAP_CLS>()->requestGC();
    return load_ptr(trueObject);
}

#ifndef FRUCTUS
static vm_oop_t sysLoadFile_(vm_oop_t /*unused*/, vm_oop_t rightObj) {
    auto* fileName = static_cast<VMString*>(rightObj);

    std::ifstream const file(fileName->GetStdString(), std::ifstream::in);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();

        VMString* result = Universe::NewString(buffer.str());
        return result;
    }
    return load_ptr(nilObject);
}
#endif

void sysPrintStackTrace(VMFrame* frame) {
    frame->PrintStackTrace();
}

// fructus has its primitives in the image, see FructusPrimitives.h
#ifndef FRUCTUS
_System::_System() {
#ifndef FRUCTUS
    gettimeofday(&start_time, nullptr);
#endif

    Add("global:", &sysGlobal_, false);
    Add("global:put:", &sysGlobalPut, false);
    Add("hasGlobal:", &sysHasGlobal_, false);
    Add("load:", &sysLoad_, false);
    Add("exit:", &sysExit_, false);
    Add("printString:", &sysPrintString_, false);
    Add("printNewline", &sysPrintNewline, false);
    Add("errorPrint:", &sysErrorPrint_, false);
    Add("errorPrintln:", &sysErrorPrintNewline_, false);
    Add("time", &sysTime, false);
    Add("ticks", &sysTicks, false);
    Add("fullGC", &sysFullGC, false);

#ifndef FRUCTUS
    Add("loadFile:", &sysLoadFile_, false);
#endif
    Add("printStackTrace", &sysPrintStackTrace, false);
}

_System::~_System() = default;
#endif
