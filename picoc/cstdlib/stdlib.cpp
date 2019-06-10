/* stdlib.h library for large systems - small embedded systems use clibrary.c instead */
#include "../interpreter.hpp"

#ifndef BUILTIN_MINI_STDLIB

static int Stdlib_ZeroValue = 0;

#ifndef NO_FP
void StdlibAtof(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = atof((const char*)Param[0]->Val->Pointer);
}
#endif

void StdlibAtoi(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = atoi((const char*)Param[0]->Val->Pointer);
}

void StdlibAtol(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = atol((const char*)Param[0]->Val->Pointer);
}

#ifndef NO_FP
void StdlibStrtod(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = strtod((const char*)Param[0]->Val->Pointer, (char**)Param[1]->Val->Pointer);
}
#endif

void StdlibStrtol(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strtol((const char*)Param[0]->Val->Pointer, (char**)Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StdlibStrtoul(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strtoul((const char*)Param[0]->Val->Pointer, (char**)Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StdlibMalloc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = calloc(Param[0]->Val->Integer, 1);
}

void StdlibCalloc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = calloc(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void StdlibRealloc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = realloc(Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void StdlibFree(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    free(Param[0]->Val->Pointer);
}

void StdlibRand(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = rand();
}

void StdlibSrand(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    srand(Param[0]->Val->Integer);
}

void StdlibAbort(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ProgramFail(Parser, "abort");
}

void StdlibExit(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    PlatformExit(Parser->pc, Param[0]->Val->Integer);
}

void StdlibGetenv(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = getenv((const char*)Param[0]->Val->Pointer);
}

void StdlibSystem(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = system((const char*)Param[0]->Val->Pointer);
}

#if 0
void StdlibBsearch(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = bsearch(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer, Param[3]->Val->Integer, (int (*)())Param[4]->Val->Pointer);
}
#endif

void StdlibAbs(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = abs(Param[0]->Val->Integer);
}

void StdlibLabs(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = labs(Param[0]->Val->Integer);
}

#if 0
void StdlibDiv(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = div(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void StdlibLdiv(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = ldiv(Param[0]->Val->Integer, Param[1]->Val->Integer);
}
#endif

#if 0
/* handy structure definitions */
const char StdlibDefs[] = "\
typedef struct { \
    int quot, rem; \
} div_t; \
\
typedef struct { \
    int quot, rem; \
} ldiv_t; \
";
#endif

/* all stdlib.h functions */
struct LibraryFunction StdlibFunctions[] =
{
#ifndef NO_FP
    { StdlibAtof,           "float atof(char *);" },
    { StdlibStrtod,         "float strtod(char *,char **);" },
#endif
    { StdlibAtoi,           "int atoi(char *);" },
    { StdlibAtol,           "int atol(char *);" },
    { StdlibStrtol,         "int strtol(char *,char **,int);" },
    { StdlibStrtoul,        "int strtoul(char *,char **,int);" },
    { StdlibMalloc,         "void *malloc(int);" },
    { StdlibCalloc,         "void *calloc(int,int);" },
    { StdlibRealloc,        "void *realloc(void *,int);" },
    { StdlibFree,           "void free(void *);" },
    { StdlibRand,           "int rand();" },
    { StdlibSrand,          "void srand(int);" },
    { StdlibAbort,          "void abort();" },
    { StdlibExit,           "void exit(int);" },
    { StdlibGetenv,         "char *getenv(char *);" },
    { StdlibSystem,         "int system(char *);" },
/*    { StdlibBsearch,        "void *bsearch(void *,void *,int,int,int (*)());" }, */
/*    { StdlibQsort,          "void *qsort(void *,int,int,int (*)());" }, */
    { StdlibAbs,            "int abs(int);" },
    { StdlibLabs,           "int labs(int);" },
#if 0
    { StdlibDiv,            "div_t div(int);" },
    { StdlibLdiv,           "ldiv_t ldiv(int);" },
#endif
    { nullptr,                 nullptr }
};

/* creates various system-dependent definitions */
void StdlibSetupFunc(Picoc *pc)
{
    /* define nullptr, TRUE and FALSE */
    if (!VariableDefined(pc, TableStrRegister(pc, "nullptr")))
        VariableDefinePlatformVar(pc, nullptr, "nullptr", &pc->IntType, (union AnyValue *)&Stdlib_ZeroValue, FALSE);
}

#endif /* !BUILTIN_MINI_STDLIB */
