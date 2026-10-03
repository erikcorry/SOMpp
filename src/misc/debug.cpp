#include "debug.h"


#ifndef FRUCTUS
#include "../compiler/Disassembler.h"
#endif
#ifndef FRUCTUS
#include "../compiler/MethodGenerationContext.h"
#endif
#include "../vmobjects/ObjectFormats.h"
#include "../vmobjects/VMClass.h"
#include "../vmobjects/VMSymbol.h"

#ifndef FRUCTUS
std::string DebugGetClassName(vm_oop_t obj) {
    return CLASS_OF(obj)->GetName()->GetStdString();
}

std::string DebugGetClassName(gc_oop_t obj) {
    return CLASS_OF(obj)->GetName()->GetStdString();
}

void DebugDumpMethodWithObjects(VMInvokable* method) {
    Disassembler::DumpMethod((VMMethod*)method, "", true);
}

void DebugDumpMethod(VMInvokable* method) {
    Disassembler::DumpMethod((VMMethod*)method, "", false);
}
#endif

#ifndef FRUCTUS
void DebugDumpMethod(MethodGenerationContext* mgenc) {
    Disassembler::DumpMethod(mgenc, "");
}
#endif
