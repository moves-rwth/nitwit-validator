/* time.h library */
#include <time.h>
#include "../interpreter.hpp"

#ifndef BUILTIN_MINI_STDLIB

static int CLOCKS_PER_SECValue = CLOCKS_PER_SEC;

#ifdef CLK_PER_SEC
static int CLK_PER_SECValue = CLK_PER_SEC;
#endif

#ifdef CLK_TCK
static int CLK_TCKValue = CLK_TCK;
#endif

void StdAsctime(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = asctime((const struct tm*)Param[0]->Val->Pointer);
}

void StdClock(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = clock();
}

void StdCtime(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = ctime((const time_t*)Param[0]->Val->Pointer);
}

#ifndef NO_FP
void StdDifftime(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = difftime((time_t)Param[0]->Val->Integer, Param[1]->Val->Integer);
}
#endif

void StdGmtime(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = gmtime((const time_t*)Param[0]->Val->Pointer);
}

void StdLocaltime(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = localtime((const time_t*)Param[0]->Val->Pointer);
}

void StdMktime(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = (int)mktime((struct tm*)Param[0]->Val->Pointer);
}

void StdTime(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = (int)time((time_t *)Param[0]->Val->Pointer);
}

void StdStrftime(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strftime((char*)Param[0]->Val->Pointer, Param[1]->Val->Integer, (const char*) Param[2]->Val->Pointer, (struct tm*) Param[3]->Val->Pointer);
}

#ifndef WIN32
void StdStrptime(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
//	  extern char *strptime(const char *s, const char *format, struct tm *tm);
	  
    ReturnValue->Val->Pointer = strptime((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer, (struct tm*) Param[2]->Val->Pointer);
}

void StdGmtime_r(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = gmtime_r((const time_t*)Param[0]->Val->Pointer, (struct tm*)Param[1]->Val->Pointer);
}

void StdTimegm(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = timegm((struct tm*)Param[0]->Val->Pointer);
}
#endif

/* handy structure definitions */
const char StdTimeDefs[] = "typedef int time_t; typedef int clock_t;";

/* all string.h functions */
struct LibraryFunction StdTimeFunctions[] =
{
    { StdAsctime,       "char *asctime(struct tm *);" },
    { StdClock,         "time_t clock();" },
    { StdCtime,         "char *ctime(int *);" },
#ifndef NO_FP
    { StdDifftime,      "double difftime(int, int);" },
#endif
    { StdGmtime,        "struct tm *gmtime(int *);" },
    { StdLocaltime,     "struct tm *localtime(int *);" },
    { StdMktime,        "int mktime(struct tm *ptm);" },
    { StdTime,          "int time(int *);" },
    { StdStrftime,      "int strftime(char *, int, char *, struct tm *);" },
#ifndef WIN32
    { StdStrptime,      "char *strptime(char *, char *, struct tm *);" },
	{ StdGmtime_r,      "struct tm *gmtime_r(int *, struct tm *);" },
    { StdTimegm,        "int timegm(struct tm *);" },
#endif
    { nullptr,             nullptr }
};


/* creates various system-dependent definitions */
void StdTimeSetupFunc(Picoc *pc)
{
    /* make a "struct tm" which is the same size as a native tm structure */
    TypeCreateOpaqueStruct(pc, nullptr, TableStrRegister(pc, "tm"), sizeof(struct tm));
    
    /* define CLK_PER_SEC etc. */
    VariableDefinePlatformVar(pc, nullptr, "CLOCKS_PER_SEC", &pc->IntType, (union AnyValue *)&CLOCKS_PER_SECValue, FALSE);
#ifdef CLK_PER_SEC
    VariableDefinePlatformVar(pc, nullptr, "CLK_PER_SEC", &pc->IntType, (union AnyValue *)&CLK_PER_SECValue, FALSE);
#endif
#ifdef CLK_TCK
    VariableDefinePlatformVar(pc, nullptr, "CLK_TCK", &pc->IntType, (union AnyValue *)&CLK_TCKValue, FALSE);
#endif
}

#endif /* !BUILTIN_MINI_STDLIB */
