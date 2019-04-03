#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/tree.h>
#include "picoc/picoc.h"
#include "utils/files.h"
#include "witness/witness.h"
#include "witness/automaton.h"

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
    xmlDocPtr doc = parseGraphmlWitness(argv[1]);
    if (doc == NULL) {
        return 1;
    }
    struct Automaton * wit_aut = automatonFromWitness(doc);

    xmlFreeDoc(doc);


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

    struct Value *MainFuncValue = NULL;
    VariableGet(&pc, NULL, TableStrRegister(&pc, "main"), &MainFuncValue);

    if (MainFuncValue->Typ->Base != TypeFunction)
        ProgramFailNoParser(&pc, "main is not a function - can't call it");

    struct ParseState ps;
    ParserCopy(&ps, &MainFuncValue->Val->FuncDef.Body);
    DebugSetBreakpoint(&ps);

    printf("Start simulation:\n\n");
    char *pc_argv[5] = {"blah"};
    PicocCallMain(&pc, NULL, 1, pc_argv);

    printf("Exit value of program: %d", pc.PicocExitValue);

    PicocCleanup(&pc);

}