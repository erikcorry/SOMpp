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

#include "Class.h"

#include "FructusPrimitives.h"

#include <string.h>

#include "../vm/Print.h"
#include "../vm/Universe.h"
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMClass.h"
#include "../vmobjects/VMFrame.h"
#include "../vmobjects/VMSymbol.h"  // NOLINT(misc-include-cleaner) it's required to make the types complete

vm_oop_t clsNew(vm_oop_t rcvr) {
    auto* self = static_cast<VMClass*>(rcvr);
    return Universe::NewInstance(self);
}

#ifdef FRUCTUS
// classes have no names on fructus, so this is class@<address>, which
// tools/fructus-trace translates
vm_oop_t clsAsString(vm_oop_t rcvr) {
    char str[16];
    FormatTagged(str, "class", (uintptr_t)rcvr);
    return Universe::NewString(strlen(str), str);
}
#else
static vm_oop_t clsName(vm_oop_t rcvr) {
    auto* self = static_cast<VMClass*>(rcvr);
    return self->GetName();
}
#endif

vm_oop_t clsSuperclass(vm_oop_t rcvr) {
    auto* self = static_cast<VMClass*>(rcvr);
    return self->GetSuperClass();
}

#ifndef FRUCTUS
// the program can't have symbols or methods on fructus
static vm_oop_t clsMethods(vm_oop_t rcvr) {
    auto* self = static_cast<VMClass*>(rcvr);
    return self->GetInstanceInvokables();
}

static vm_oop_t clsFields(vm_oop_t rcvr) {
    auto* self = static_cast<VMClass*>(rcvr);
    return self->GetInstanceFields();
}
#endif

// fructus has its primitives in the image, see FructusPrimitives.h
#ifndef FRUCTUS
_Class::_Class() {
    Add("new", &clsNew, false);
    Add("name", &clsName, false);
    Add("superclass", &clsSuperclass, false);
    Add("fields", &clsFields, false);
    Add("methods", &clsMethods, false);
}
#endif
