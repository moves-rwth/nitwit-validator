#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "picoc/picoc.h"
}
#undef min

#include "utils/files.hpp"
#include "witness/witness.hpp"
#include "witness/automaton.hpp"
#include "program_state.hpp"
#include "utils/extern_verif_defs.hpp"

using namespace std;
shared_ptr<Automaton> wit_aut;

// the values shouldn't conflict with any real program exit value as validation ends before returning for these error codes
// Only if program finishes with value PROGRAM_FINISHED, then it still doesn't matter, because this would mean
// that it did not get validated
int NO_WITNESS_CODE = 0xadf0;
int WITNESS_IN_ILLEGAL_STATE = 0xadf1;
int WITNESS_IN_SINK = 0xadf2;
int PROGRAM_FINISHED = 0xadf3;

void handleDebugBreakpoint(struct ParseState *ps) {
    if (ps->LastConditionBranch == ConditionUndefined) printf("%s --- Line: %zu, Pos: %d\n", ps->FileName, ps->Line, ps->CharacterPos);
    else printf("%s --- Line: %zu, Pos: %d, Control: %d\n", ps->FileName, ps->Line, ps->CharacterPos, ps->LastConditionBranch == ConditionTrue);

    if (wit_aut == nullptr) {
        ProgramFailWithExitCode(ps, NO_WITNESS_CODE, "No witness automaton to validate against.\n");
        return;
    }
    if (wit_aut->isInIllegalState()) {
        ProgramFailWithExitCode(ps, WITNESS_IN_ILLEGAL_STATE, "Witness automaton is in an illegal state.\n");
        return;
    }
    if (wit_aut->isInSinkState()) {
        ProgramFailWithExitCode(ps, WITNESS_IN_SINK, "Witness automaton reached sink state without a violation.\n");
        return;
    }

    wit_aut->consumeState(ps);

    if (wit_aut.get()->isInViolationState()) {
        PlatformExit(ps->pc, 0);
        return;
    }
}

int validate(const char *source_filename) {

    Picoc pc;
    PicocInitialise(&pc, 8388608); // stack size of 8 MiB

    PicocIncludeAllSystemHeaders(&pc);

    // the interpreter will jump here after finding a violation
    if (PicocPlatformSetExitPoint(&pc)) {
        printf("===============Finished=================\n");
        printf("Stopping the interpreter.\n");
        int ret = pc.PicocExitValue;
        PicocCleanup(&pc);

        return ret;
    }
    printf("============Start simulation============\n");
    char *source = readFile(source_filename);
    PicocParse(&pc, source_filename, source, strlen(source),
               TRUE, FALSE, TRUE, TRUE, handleDebugBreakpoint);
    // also include extern functions used by verifiers like error, assume, nondet...
    PicocParse(&pc, EXTERN_C_DEFS_FILENAME, EXTERN_C_DEFS_FOR_VERIFIERS, strlen(EXTERN_C_DEFS_FOR_VERIFIERS),
               TRUE, FALSE, FALSE, FALSE, handleDebugBreakpoint);

    if (!VariableDefined(&pc, TableStrRegister(&pc, "main")))
        printf("Sorry, not sorry. No main function...");

    struct Value *MainFuncValue = nullptr;
    VariableGet(&pc, nullptr, TableStrRegister(&pc, "main"), &MainFuncValue);

    if (MainFuncValue->Typ->Base != TypeFunction)
        ProgramFailNoParser(&pc, "main is not a function - can't call it");

    PicocCallMain(&pc, nullptr, 0, nullptr);
    printf("===============Finished=================\n\n");

    printf("Program finished. Exit value: %d\n", pc.PicocExitValue);

    PicocCleanup(&pc);
    return PROGRAM_FINISHED;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: <cwvalidator> witness.graphml source-file.c");
        return 3;
    }

    auto doc = parseGraphmlWitness(argv[1]);
    if (doc == nullptr) {
        return 2;
    }
    wit_aut = Automaton::automatonFromWitness(doc);

    if (wit_aut && !wit_aut->isInIllegalState()) {
//        wit_aut->printData();
//        wit_aut->printRelations();
    } else {
        printf("Reconstructing the witness automaton failed.\n");
        return 2;
    }
    int exit_value = validate(argv[2]);
    if (!wit_aut->isInViolationState() &&
        (exit_value == PROGRAM_FINISHED || exit_value == NO_WITNESS_CODE || exit_value == WITNESS_IN_ILLEGAL_STATE || exit_value == WITNESS_IN_SINK)) {
        printf("FAILED: Wasn't able to validate the witness. Violation NOT reached.\n");
        printf("Automaton finished in state %s, with error code %d\n", wit_aut->getCurrentState()->id.c_str(), exit_value);
        return 1;
    } else if (!wit_aut->isInViolationState()) {
        printf("A different error occurred, probably a parsing error.\n");
        return 4;
    }
    printf("\nVALIDATED: The violation state: %s has been reached.\n", wit_aut->getCurrentState()->id.c_str());
    return 0;
}

