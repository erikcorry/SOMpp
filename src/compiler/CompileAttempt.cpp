#include "CompileAttempt.h"

bool CompileAttempt::trying = false;
size_t CompileAttempt::failures = 0;

void AbandonIfCompiling() {
    if (CompileAttempt::trying) {
        throw CompilationAbandoned{};
    }
}
