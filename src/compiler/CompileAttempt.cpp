#include "CompileAttempt.h"

bool CompileAttempt::trying = false;
bool CompileAttempt::failedOnce = false;

void AbandonIfCompiling() {
    if (CompileAttempt::trying) {
        throw CompilationAbandoned{};
    }
}
