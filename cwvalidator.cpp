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

using namespace std;

shared_ptr<Automaton> wit_aut;

// the values shouldn't conflict with any real program exit value as validation ends before returning for these error codes
// Only if program finishes with value PROGRAM_FINISHED, then it still doesn't matter, because this would mean
// that it did not get validated
int NO_WITNESS_CODE = 240;
int WITNESS_IN_SINK = 241;
int PROGRAM_FINISHED = 242;
int WITNESS_IN_ILLEGAL_STATE = 243;
int IDENTIFIER_UNDEFINED = 244;
int PROGRAM_FINISHED_WITH_VIOLATION = 245;
int ALREADY_DEFINED = 246;
int UNSUPPORTED_NONDET_RESOLUTION_OP = 247;
int ASSERTION_FAILED = 248;
int BAD_FUNCTION_DEF = 249;
int UNVALIDATED_VIOLATION = 250;

void printProgramState(ParseState *ps) {
    printf("%s --- Line: %zu, Pos: %d", ps->FileName, ps->Line, ps->CharacterPos);
    if (ps->LastConditionBranch != ConditionUndefined)
        printf(", Control: %d", ps->LastConditionBranch == ConditionTrue);
    if (ps->EnterFunction != nullptr)
        printf(", Enter: %s", ps->EnterFunction);
    if (ps->ReturnFromFunction != nullptr)
        printf(", Return: %s", ps->ReturnFromFunction);
    printf("\n");
    if (ps->Line == 375 && ps->CharacterPos == 0) {
        printf("debug\n");
    }
}

void handleDebugBreakpoint(struct ParseState *ps) {
#ifdef VERBOSE
    printProgramState(ps);
#endif
    if (ps->SkipIntrinsic == TRUE)
        return;
    if (wit_aut == nullptr) {
        ProgramFailWithExitCode(ps, NO_WITNESS_CODE, "No witness automaton to validate against.");
        return;
    }
    if (wit_aut->isInIllegalState()) {
        ProgramFailWithExitCode(ps, WITNESS_IN_ILLEGAL_STATE, "Witness automaton is in an illegal state.");
        return;
    }
    if (wit_aut->isInSinkState()) {
        ProgramFailWithExitCode(ps, WITNESS_IN_SINK, "Witness automaton reached sink state without a violation.");
        return;
    }

    wit_aut->consumeState(ps);

    if (wit_aut->isInViolationState() && wit_aut->wasVerifierErrorCalled()) {
        PlatformExit(ps->pc, 0);
        return;
    }
}

int validate(const char *source_filename) {

    Picoc pc;
    PicocInitialise(&pc, 8388608); // stack size of 8 MiB

    // the interpreter will jump here after finding a violation
    if (PicocPlatformSetExitPoint(&pc)) {
        cw_verbose("===============Finished=================\n");
        cw_verbose("Stopping the interpreter.\n");
        int ret = pc.PicocExitValue;
        PicocCleanup(&pc);

        return ret;
    }
    cw_verbose("============Start simulation============\n");
    char defs[] = "_Bool.h";
    IncludeFile(&pc, defs);
    char *source = readFile(source_filename);
    PicocParse(&pc, source_filename, source, strlen(source),
               TRUE, FALSE, TRUE, TRUE, handleDebugBreakpoint);
    // also include extern functions used by verifiers like error, assume, nondet...
    char fn[] = "verif.h";
    IncludeFile(&pc, fn);

    if (!VariableDefined(&pc, TableStrRegister(&pc, "main")))
        printf("Sorry, not sorry. No main function...");

    struct Value *MainFuncValue = nullptr;
    VariableGet(&pc, nullptr, TableStrRegister(&pc, "main"), &MainFuncValue);

    if (MainFuncValue->Typ->Base != TypeFunction)
        ProgramFailNoParser(&pc, "main is not a function - can't call it");

    PicocCallMain(&pc, nullptr, 0, nullptr);
    cw_verbose("===============Finished=================\n\n");

    cw_verbose("Program finished. Exit value: %d\n", pc.PicocExitValue);

    PicocCleanup(&pc);
    return PROGRAM_FINISHED;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: <cwvalidator> witness.graphml source-file.c\n");
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
    if ((!wit_aut->isInViolationState() || !wit_aut->wasVerifierErrorCalled()) &&
        (exit_value >= NO_WITNESS_CODE && exit_value <= ALREADY_DEFINED)) {
        cw_verbose("Automaton finished in state %s, with error code %d.\n", wit_aut->getCurrentState()->id.c_str(),
                   exit_value);
        cw_verbose("FAILED: Wasn't able to validate the witness. ");
        if (wit_aut->isInViolationState()) {
            cw_verbose("Witness violation state reached");
            exit_value = UNVALIDATED_VIOLATION;
        } else{
            cw_verbose("Witness violation state NOT reached");
        }
        if (wit_aut->wasVerifierErrorCalled()) {
            cw_verbose(", __VERIFIER_error was called.\n");
            exit_value = PROGRAM_FINISHED_WITH_VIOLATION;
        } else {
            cw_verbose(", __VERIFIER_error was never called.\n");
        }
        return exit_value;
    } else if (wit_aut->isInViolationState() && !wit_aut->wasVerifierErrorCalled()) {
        cw_verbose("FAILED: __VERIFIER_error was never called, even though witness IS in violation state.\n");
        return 5;
    } else if (wit_aut->isInViolationState() && wit_aut->wasVerifierErrorCalled()) {
        printf("\nVALIDATED: The violation state: %s has been reached.\n", wit_aut->getCurrentState()->id.c_str());
        return 0;
    } else {
        cw_verbose("UNKNOWN: A different error occurred, probably a parsing error or program exited.\n");
        return 4;
    }
}

