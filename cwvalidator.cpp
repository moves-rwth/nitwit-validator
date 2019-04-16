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

void tryOutDebug(const char *source_filename);


string baseFileName(const char *path) {
    string s(path);
    return s.substr(s.find_last_of("/\\") + 1);
}

void handleDebug(struct ParseState *ps) {
    printf("File: %s ----- Line: %d, Pos: %d\n", ps->FileName, ps->Line, ps->CharacterPos);
    if (wit_aut == nullptr) {
        ProgramFail(ps, "No witness automaton to validate against.\n");
        return;
    }
    if (wit_aut->isInIllegalState()) {
        ProgramFail(ps, "Witness automaton is in an illegal state.\n");
        return;
    }

    ProgramState program_state(baseFileName(ps->FileName), "", "", "", "", ps->Line, false);
    wit_aut->consumeState(program_state);

    if (wit_aut.get()->isInViolationState()) {
        printf("Violation reached!\n");
        ProgramFail(ps, "Property has been violated! Ending validation, witness is correct.\n");
        return;
    }
}

void tryOutDebug(const char *source_filename) {

    Picoc pc;
    PicocInitialise(&pc, 8388608); // stack size of 8 MiB

    PicocIncludeAllSystemHeaders(&pc);
    // also include extern functions used by verifiers like error, assume, nondet...
//    PicocParse(&pc, EXTERN_C_DEFS_FILENAME, EXTERN_C_DEFS_FOR_VERIFIERS, strlen(EXTERN_C_DEFS_FOR_VERIFIERS),
//               TRUE, FALSE, TRUE, TRUE, handleDebug);
    char *source = readFile(source_filename);
    printf("Analyzing:\n%s", source);
    PicocParse(&pc, source_filename, source, strlen(source),
               TRUE, FALSE, TRUE, TRUE, handleDebug);

    if (!VariableDefined(&pc, TableStrRegister(&pc, "main")))
        printf("Sorry, not sorry. No main function...");

    Value *MainFuncValue = nullptr;
    VariableGet(&pc, nullptr, TableStrRegister(&pc, "main"), &MainFuncValue);

    if (MainFuncValue->Typ->Base != TypeFunction)
        ProgramFailNoParser(&pc, "main is not a function - can't call it");

    auto *ps = new ParseState();
    ParserCopy(ps, &MainFuncValue->Val->FuncDef.Body);
    DebugSetBreakpoint(ps);

    printf("================================\nStart simulation:\n\n");

    PicocCallMain(&pc, nullptr, 1, nullptr);

    printf("Exit value of program: %d\n", pc.PicocExitValue);

    PicocCleanup(&pc);
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
        wit_aut->printData();
        wit_aut->printRelations();
    } else {
        printf("Reconstructing the witness automaton failed.\n");
        return 1;
    }
    tryOutDebug(argv[2]);

    return 0;
}

