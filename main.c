#include <stdio.h>
#include "picoc/picoc.h"

void handleDebug(const struct ParseState* ps) {
    printf("Line: %d, Pos: %d\n", ps->Line, ps->CharacterPos);
}

void tryOutPicoc() {
    Picoc pc;
    PicocInitialise(&pc, 8388608); // stack size of 8 MiB

    PicocParse(&pc, "blah.c", "int main() {\nreturn 42;\n}\n", 25,
               TRUE, FALSE, FALSE, TRUE, handleDebug);

    if (!VariableDefined(&pc, TableStrRegister(&pc, "main")))
        printf("Sorry, not sorry. No main function...");

    struct Value *MainFuncValue = NULL;
    VariableGet(&pc, NULL, TableStrRegister(&pc, "main"), &MainFuncValue);

    if (MainFuncValue->Typ->Base != TypeFunction)
        ProgramFailNoParser(&pc, "main is not a function - can't call it");

    struct ParseState ps;
    ParserCopy(&ps, &MainFuncValue->Val->FuncDef.Body);
    DebugSetBreakpoint(&ps);

    char *pc_argv[5] = {"blah"};
    PicocCallMain(&pc, NULL, 1, pc_argv);

    printf("Exit value of program: %d", pc.PicocExitValue);

    PicocCleanup(&pc);
}

int main(int argc, char **argv) {
//    if (argc < 2) {
//        printf("Usage: <cwvalidator> source-file.c");
//        return 1;
//    }

    tryOutPicoc();
    return 0;
}
