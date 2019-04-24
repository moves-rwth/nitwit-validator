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



void handleDebugBreakpoint(struct ParseState *ps) {
    printf("File: %s ----- Line: %d, Pos: %d\n", ps->FileName, ps->Line, ps->CharacterPos);
    if (wit_aut == nullptr) {
        ProgramFail(ps, "No witness automaton to validate against.\n");
        return;
    }
    if (wit_aut->isInIllegalState()) {
        ProgramFail(ps, "Witness automaton is in an illegal state.\n");
        return;
    }

    if (wit_aut->isInSinkState()) {
        ProgramFail(ps, "Witness automaton reached sink state without a violation.\n");
        return;
    }

    auto *program_state = new ProgramState(ps->FileName, "", "", "", ConditionUndefined, ps->Line, false);
    wit_aut->consumeState(*program_state);
    delete program_state;

    if (wit_aut.get()->isInViolationState()) {
        printf("\nVALIDATED: The violation state: %s has been reached.\n", wit_aut->getCurrentState()->id.c_str());
        PlatformExit(ps->pc, 1);
        return;
    }
}

bool validate(const char *source_filename) {

    Picoc pc;
    PicocInitialise(&pc, 8388608); // stack size of 8 MiB

    PicocIncludeAllSystemHeaders(&pc);

    // the interpreter will jump here after finding a violation
    if (PicocPlatformSetExitPoint(&pc)) {
        printf("Stopping the interpreter.\n");
        printf("===============Finished=================\n\n");
        PicocCleanup(&pc);

        return false;
    }
    char *source = readFile(source_filename);
//    printf("Analyzing:\n%s", source);
    PicocParse(&pc, source_filename, source, strlen(source),
               TRUE, FALSE, TRUE, TRUE, handleDebugBreakpoint);
    // also include extern functions used by verifiers like error, assume, nondet...
    PicocParse(&pc, EXTERN_C_DEFS_FILENAME, EXTERN_C_DEFS_FOR_VERIFIERS, strlen(EXTERN_C_DEFS_FOR_VERIFIERS),
               TRUE, FALSE, FALSE, TRUE, handleDebugBreakpoint);

    if (!VariableDefined(&pc, TableStrRegister(&pc, "main")))
        printf("Sorry, not sorry. No main function...");

    struct Value *MainFuncValue = nullptr;
    VariableGet(&pc, nullptr, TableStrRegister(&pc, "main"), &MainFuncValue);

    if (MainFuncValue->Typ->Base != TypeFunction)
        ProgramFailNoParser(&pc, "main is not a function - can't call it");

    auto *ps = new ParseState();
    ParserCopy(ps, &MainFuncValue->Val->FuncDef.Body);
    ps->ScopeID = MainFuncValue->ScopeID;
    DebugSetBreakpoint(ps);
    delete ps;


    printf("============Start simulation============\n");
    PicocCallMain(&pc, nullptr, 1, nullptr);
    printf("===============Finished=================\n\n");

    printf("Exit value of the program: %d\n", pc.PicocExitValue);

    PicocCleanup(&pc);
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: <cwvalidator> witness.graphml source-file.c");
        return 1;
    }

    auto doc = parseGraphmlWitness(argv[1]);
    if (doc == nullptr) {
        return 1;
    }
    wit_aut = Automaton::automatonFromWitness(doc);

    if (wit_aut && !wit_aut->isInIllegalState()) {
//        wit_aut->printData();
//        wit_aut->printRelations();
    } else {
        printf("Reconstructing the witness automaton failed.\n");
        return 1;
    }
    if (!validate(argv[2]) && !wit_aut->isInViolationState()) {
        printf("FAILED: Wasn't able to validate the witness. Violation NOT reached.\n");
        printf("Automaton finished in state: %s\n", wit_aut->getCurrentState()->id.c_str());
        return 1;
    }
    return 0;
}

