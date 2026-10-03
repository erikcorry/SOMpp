// fructus' crt headers lack extern "C" guards, so C++ would mangle the names.
// Like the C header, this can be included repeatedly with different NDEBUG.
extern "C" {
#include_next <assert.h>
}
