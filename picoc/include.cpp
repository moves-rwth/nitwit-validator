/* picoc include system - can emulate system includes from built-in libraries
 * or it can include and parse files if the system has files */

#include "picoc.hpp"
#include "interpreter.hpp"

#ifndef NO_HASH_INCLUDE


/* initialise the built-in include libraries */
void IncludeInit(Picoc *pc) {
#ifndef BUILTIN_MINI_STDLIB
    IncludeRegister(pc, "ctype.h", nullptr, &StdCTypeFunctions[0], nullptr);
    IncludeRegister(pc, "errno.h", &StdErrnoSetupFunc, nullptr, nullptr);
    IncludeRegister(pc, "math.h", &MathSetupFunc, &MathFunctions[0], nullptr);
    IncludeRegister(pc, "stdbool.h", &StdboolSetupFunc, nullptr, StdboolDefs);
    IncludeRegister(pc, "stdio.h", &StdioSetupFunc, &StdioFunctions[0], StdioDefs);
    IncludeRegister(pc, "stdlib.h", &StdlibSetupFunc, &StdlibFunctions[0], nullptr);
    IncludeRegister(pc, "string.h", &StringSetupFunc, &StringFunctions[0], nullptr);
    IncludeRegister(pc, "time.h", &StdTimeSetupFunc, &StdTimeFunctions[0], StdTimeDefs);
# ifndef WIN32
    IncludeRegister(pc, "unistd.h", &UnistdSetupFunc, &UnistdFunctions[0], UnistdDefs);
# endif

    IncludeRegister(pc, "assert.h", nullptr, &AssertFunctions[0], nullptr);
    // verification library
    IncludeRegister(pc, "verif.h", &VerifSetupFunc, &VerifFunctions[0], nullptr);
    IncludeRegister(pc, "_Bool.h", nullptr, nullptr, VerifDefs);
#endif
}

/* clean up space used by the include system */
void IncludeCleanup(Picoc *pc) {
    struct IncludeLibrary *ThisInclude = pc->IncludeLibList;
    struct IncludeLibrary *NextInclude;

    while (ThisInclude != nullptr) {
        NextInclude = ThisInclude->NextLib;
        HeapFreeMem(pc, ThisInclude);
        ThisInclude = NextInclude;
    }

    pc->IncludeLibList = nullptr;
}

/* register a new build-in include file */
void
IncludeRegister(Picoc *pc, const char *IncludeName, void (*SetupFunction)(Picoc *pc), struct LibraryFunction *FuncList,
                const char *SetupCSource) {
    auto *NewLib = static_cast<IncludeLibrary *>(HeapAllocMem(pc, sizeof(struct IncludeLibrary)));
    NewLib->IncludeName = nitwit::table::TableStrRegister(pc, IncludeName);
    NewLib->SetupFunction = SetupFunction;
    NewLib->FuncList = FuncList;
    NewLib->SetupCSource = SetupCSource;
    NewLib->NextLib = pc->IncludeLibList;
    pc->IncludeLibList = NewLib;
}

/* include all of the system headers */
void PicocIncludeAllSystemHeaders(Picoc *pc) {
    struct IncludeLibrary *ThisInclude = pc->IncludeLibList;

    for (; ThisInclude != nullptr; ThisInclude = ThisInclude->NextLib) {
        IncludeFile(pc, ThisInclude->IncludeName);
    }
}

/* include one of a number of predefined libraries, or perhaps an actual file */
void IncludeFile(Picoc *pc, char *FileName) {
    struct IncludeLibrary *LInclude;

    /* scan for the include file name to see if it's in our list of predefined includes */
    for (LInclude = pc->IncludeLibList; LInclude != nullptr; LInclude = LInclude->NextLib) {
        if (strcmp(LInclude->IncludeName, FileName) == 0) {
            /* found it - protect against multiple inclusion */
            if (!VariableDefined(pc, FileName)) {
                VariableDefine(pc, nullptr, FileName, nullptr, &pc->VoidType, FALSE, false);

                /* run an extra startup function if there is one */
                if (LInclude->SetupFunction != nullptr)
                    (*LInclude->SetupFunction)(pc);

                /* parse the setup C source code - may define types etc. */
                if (LInclude->SetupCSource != nullptr) {
                    nitwit::parse::PicocParse(pc, FileName, LInclude->SetupCSource, strlen(LInclude->SetupCSource), TRUE, TRUE, FALSE, FALSE, nullptr);
                }

                /* set up the library functions */
                if (LInclude->FuncList != nullptr)
                    LibraryAdd(pc, &pc->GlobalTable, FileName, LInclude->FuncList);
            }

            return;
        }
    }

    /* not a predefined file, read a real file */
    PicocPlatformScanFile(pc, FileName);
}

#endif /* NO_HASH_INCLUDE */
