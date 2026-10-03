// fructus' crt headers lack extern "C" guards, so C++ would mangle the names
#pragma once
extern "C" {
#include_next <string.h>
}
