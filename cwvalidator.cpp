#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
    #include "picoc/picoc.h"
}
#undef min

#include "utils/files.hpp"
#include "witness/witness.hpp"
#include "witness/automaton_parse.hpp"

using namespace std;

void handleDebug(const struct ParseState* ps) {
    printf("Line: %d, Pos: %d\n", ps->Line, ps->CharacterPos);
}

void tryOutDebug(const char * source_filename);


int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: <cwvalidator> source-file.c");
        return 1;
    }

//    tryOutDebug(argv[1]);
    auto *doc = parseGraphmlWitness(argv[1]);
    if (doc == nullptr) {
        return 1;
    }
    auto wit_aut = automatonFromWitness(*doc);

    if (wit_aut) {
        wit_aut->data->print();
    } else {
        printf("Reconstructing the witness automaton failed.\n");
    }
    delete doc;
    return 0;
}


void tryOutDebug(const char * source_filename){

    Picoc pc;
    PicocInitialise(&pc, 8388608); // stack size of 8 MiB

    PicocIncludeAllSystemHeaders(&pc);
    char *source = readFile(source_filename);
    printf("Analyzing:\n%s", source);
    PicocParse(&pc, source_filename, source, strlen(source),
               TRUE, FALSE, TRUE, TRUE, handleDebug);

    if (!VariableDefined(&pc, TableStrRegister(&pc, "main")))
        printf("Sorry, not sorry. No main function...");

    struct Value *MainFuncValue = nullptr;
    VariableGet(&pc, nullptr, TableStrRegister(&pc, "main"), &MainFuncValue);

    if (MainFuncValue->Typ->Base != TypeFunction)
        ProgramFailNoParser(&pc, "main is not a function - can't call it");

    auto * ps = new ParseState();
    ParserCopy(ps, &MainFuncValue->Val->FuncDef.Body);
    DebugSetBreakpoint(ps);

    printf("================================\nStart simulation:\n\n");

    PicocCallMain(&pc, nullptr, 1, nullptr);

    printf("Exit value of program: %d\n", pc.PicocExitValue);

    PicocCleanup(&pc);
}