/* string.h library for large systems - small embedded systems use clibrary.c instead */
#include "../interpreter.hpp"

#ifndef BUILTIN_MINI_STDLIB

static int String_ZeroValue = 0;

void StringStrcpy(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strcpy((char*)Param[0]->Val->Pointer,(const char*) Param[1]->Val->Pointer);
}

void StringStrncpy(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strncpy((char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringStrcmp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strcmp((const char*)Param[0]->Val->Pointer, (const char*) Param[1]->Val->Pointer);
}

void StringStrncmp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strncmp((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringStrcat(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strcat((char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer);
}

void StringStrncat(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strncat((char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

#ifndef WIN32
void StringIndex(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = (void *) index((const char *) Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void StringRindex(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = (void *) rindex((const char *) Param[0]->Val->Pointer, Param[1]->Val->Integer);
}
#endif

void StringStrlen(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strlen((const char*)Param[0]->Val->Pointer);
}

void StringMemset(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = memset(Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void StringMemcpy(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = memcpy(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringMemcmp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = memcmp(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringMemmove(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = memmove(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringMemchr(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = memchr(Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void StringStrchr(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = (void *) strchr((const char *) Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void StringStrrchr(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = (void *) strrchr((const char *) Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void StringStrcoll(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strcoll((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer);
}

void StringStrerror(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strerror(Param[0]->Val->Integer);
}

void StringStrspn(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strspn((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer);
}

void StringStrcspn(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strcspn((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer);
}

void StringStrpbrk(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = (void *) strpbrk((const char *) Param[0]->Val->Pointer,
                                                 (const char *) Param[1]->Val->Pointer);
}

void StringStrstr(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = (void *) strstr((const char *) Param[0]->Val->Pointer,
                                                (const char *) Param[1]->Val->Pointer);
}

void StringStrtok(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strtok((char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer);
}

void StringStrxfrm(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = strxfrm((char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

#ifndef WIN32
void StringStrdup(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strdup((const char*)Param[0]->Val->Pointer);
}

void StringStrtok_r(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strtok_r((char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer, (char**)Param[2]->Val->Pointer);
}
#endif

/* all string.h functions */
struct LibraryFunction StringFunctions[] =
{
#ifndef WIN32
	{ StringIndex,         "char *index(char *,int);" },
    { StringRindex,        "char *rindex(char *,int);" },
#endif
    { StringMemcpy,        "void *memcpy(void *,void *,int);" },
    { StringMemmove,       "void *memmove(void *,void *,int);" },
    { StringMemchr,        "void *memchr(char *,int,int);" },
    { StringMemcmp,        "int memcmp(void *,void *,int);" },
    { StringMemset,        "void *memset(void *,int,int);" },
    { StringStrcat,        "char *strcat(char *,char *);" },
    { StringStrncat,       "char *strncat(char *,char *,int);" },
    { StringStrchr,        "char *strchr(char *,int);" },
    { StringStrrchr,       "char *strrchr(char *,int);" },
    { StringStrcmp,        "int strcmp(char *,char *);" },
    { StringStrncmp,       "int strncmp(char *,char *,int);" },
    { StringStrcoll,       "int strcoll(char *,char *);" },
    { StringStrcpy,        "char *strcpy(char *,char *);" },
    { StringStrncpy,       "char *strncpy(char *,char *,int);" },
    { StringStrerror,      "char *strerror(int);" },
    { StringStrlen,        "int strlen(char *);" },
    { StringStrspn,        "int strspn(char *,char *);" },
    { StringStrcspn,       "int strcspn(char *,char *);" },
    { StringStrpbrk,       "char *strpbrk(char *,char *);" },
    { StringStrstr,        "char *strstr(char *,char *);" },
    { StringStrtok,        "char *strtok(char *,char *);" },
    { StringStrxfrm,       "int strxfrm(char *,char *,int);" },
#ifndef WIN32
	{ StringStrdup,        "char *strdup(char *);" },
    { StringStrtok_r,      "char *strtok_r(char *,char *,char **);" },
#endif
    { nullptr,             nullptr }
};

/* creates various system-dependent definitions */
void StringSetupFunc(Picoc *pc)
{
    /* define nullptr */
    if (!VariableDefined(pc, TableStrRegister(pc, "nullptr")))
        VariableDefinePlatformVar(pc, nullptr, "nullptr", &pc->IntType, (union AnyValue *)&String_ZeroValue, FALSE);
}

#endif /* !BUILTIN_MINI_STDLIB */
