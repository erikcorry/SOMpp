#include "CompileAttempt.h"

bool CompileAttempt::trying = false;
bool CompileAttempt::failedOnce = false;

void AbandonCompilationIfTrying() {
    if (CompileAttempt::trying) {
        throw CompilationAbandoned{};
    }
}
