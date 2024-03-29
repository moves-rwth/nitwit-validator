/* picoc mini standard C library - provides an optional tiny C standard library 
 * if BUILTIN_MINI_STDLIB is defined */ 
 
#include "picoc.hpp"
#include "interpreter.hpp"


/* endian-ness checking */
static const int __ENDIAN_CHECK__ = 1;
static int BigEndian;
static int LittleEndian;


/* global initialisation for libraries */
void LibraryInit(Picoc *pc)
{
    
    /* define the version number macro */
    pc->VersionString = nitwit::table::TableStrRegister(pc, PICOC_VERSION);
    VariableDefinePlatformVar(pc, nullptr, "PICOC_VERSION", pc->CharPtrType, (union AnyValue *)&pc->VersionString, FALSE);

    /* define endian-ness macros */
    BigEndian = ((*(char*)&__ENDIAN_CHECK__) == 0);
    LittleEndian = ((*(char*)&__ENDIAN_CHECK__) == 1);

    VariableDefinePlatformVar(pc, nullptr, "BIG_ENDIAN", &pc->IntType, (union AnyValue *)&BigEndian, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "LITTLE_ENDIAN", &pc->IntType, (union AnyValue *)&LittleEndian, FALSE);
}

/* add a library */
void LibraryAdd(Picoc *pc, struct Table *GlobalTable, const char *LibraryName, struct LibraryFunction *FuncList)
{
    struct ParseState Parser;
    int Count;
    char *Identifier;
    struct ValueType *ReturnType;
    Value *NewValue;
    void *Tokens;
    char *IntrinsicName = nitwit::table::TableStrRegister(pc, "c library");
    
    /* read all the library definitions */
    for (Count = 0; FuncList[Count].Prototype != nullptr; Count++)
    {
        Tokens = nitwit::lex::LexAnalyse(pc, IntrinsicName, FuncList[Count].Prototype, static_cast<int>(strlen((char *)FuncList[Count].Prototype)), nullptr);
        nitwit::lex::LexInitParser(&Parser, pc, FuncList[Count].Prototype, Tokens, IntrinsicName, TRUE, FALSE, nullptr);
        TypeParse(&Parser, &ReturnType, &Identifier, nullptr, nullptr, false);
        NewValue = nitwit::parse::ParseFunctionDefinition(&Parser, ReturnType, Identifier, false);
        NewValue->Val->FuncDef.Intrinsic = FuncList[Count].Func;
        HeapFreeMem(pc, Tokens);
    }
}

/* print a type to a stream without using printf/sprintf */
void PrintType(struct ValueType *Typ, IOFILE *Stream)
{
    switch (Typ->Base)
    {
        case BaseType::TypeVoid:              PrintStr("void", Stream); break;
        case BaseType::TypeInt:               PrintStr("int", Stream); break;
        case BaseType::TypeShort:             PrintStr("short", Stream); break;
        case BaseType::TypeChar:              PrintStr("char", Stream); break;
        case BaseType::TypeLong:              PrintStr("long", Stream); break;
        case BaseType::TypeLongLong:          PrintStr("long long", Stream); break;
        case BaseType::TypeUnsignedInt:       PrintStr("unsigned int", Stream); break;
        case BaseType::TypeUnsignedShort:     PrintStr("unsigned short", Stream); break;
        case BaseType::TypeUnsignedLong:      PrintStr("unsigned long", Stream); break;
        case BaseType::TypeUnsignedLongLong:  PrintStr("unsigned long long", Stream); break;
        case BaseType::TypeUnsignedChar:      PrintStr("unsigned char", Stream); break;
        case BaseType::TypeDouble:            PrintStr("double", Stream); break;
        case BaseType::TypeFloat:             PrintStr("float", Stream); break;
        case BaseType::TypeFunction:          PrintStr("function", Stream); break;
        case BaseType::TypeMacro:             PrintStr("macro", Stream); break;
        case BaseType::TypePointer:           if (Typ->FromType) PrintType(Typ->FromType, Stream); PrintCh('*', Stream); break;
        case BaseType::TypeArray:             PrintType(Typ->FromType, Stream); PrintCh('[', Stream); if (Typ->ArraySize != 0) PrintSimpleInt(Typ->ArraySize, Stream); PrintCh(']', Stream); break;
        case BaseType::TypeStruct:            PrintStr("struct ", Stream); PrintStr( Typ->Identifier, Stream); break;
        case BaseType::TypeUnion:             PrintStr("union ", Stream); PrintStr(Typ->Identifier, Stream); break;
        case BaseType::TypeEnum:              PrintStr("enum ", Stream); PrintStr(Typ->Identifier, Stream); break;
        case BaseType::TypeGotoLabel:         PrintStr("goto label ", Stream); break;
        case BaseType::Type_Type:             PrintStr("type ", Stream); break;
        case BaseType::TypeFunctionPtr:       PrintStr("fptr ", Stream); break;
    }
}


#ifdef BUILTIN_MINI_STDLIB

/* 
 * This is a simplified standard library for small embedded systems. It doesn't require
 * a system stdio library to operate.
 *
 * A more complete standard library for larger computers is in the library_XXX.c files.
 */

static int TRUEValue = 1;
static int ZeroValue = 0;

void BasicIOInit(Picoc *pc)
{
    pc->CStdOutBase.Putch = &PlatformPutc;
    pc->CStdOut = &CStdOutBase;
}

/* initialise the C library */
void CLibraryInit(Picoc *pc)
{
    /* define some constants */
    VariableDefinePlatformVar(pc, nullptr, "nullptr", &IntType, (union AnyValue *)&ZeroValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "TRUE", &IntType, (union AnyValue *)&TRUEValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "FALSE", &IntType, (union AnyValue *)&ZeroValue, FALSE);
}

/* stream for writing into strings */
void SPutc(unsigned char Ch, union OutputStreamInfo *Stream)
{
    struct StringOutputStream *Out = &Stream->Str;
    *Out->WritePos++ = Ch;
}

/* print a character to a stream without using printf/sprintf */
void PrintCh(char OutCh, struct OutputStream *Stream)
{
    (*Stream->Putch)(OutCh, &Stream->i);
}

/* print a string to a stream without using printf/sprintf */
void PrintStr(const char *Str, struct OutputStream *Stream)
{
    while (*Str != 0)
        PrintCh(*Str++, Stream);
}

/* print a single character a given number of times */
void PrintRepeatedChar(Picoc *pc, char ShowChar, int Length, struct OutputStream *Stream)
{
    while (Length-- > 0)
        PrintCh(ShowChar, Stream);
}

/* print an unsigned integer to a stream without using printf/sprintf */
void PrintUnsigned(unsigned long Num, unsigned int Base, int FieldWidth, int ZeroPad, int LeftJustify, struct OutputStream *Stream)
{
    char Result[33];
    int ResPos = sizeof(Result);

    Result[--ResPos] = '\0';
    if (Num == 0)
        Result[--ResPos] = '0';
            
    while (Num > 0)
    {
        unsigned long NextNum = Num / Base;
        unsigned long Digit = Num - NextNum * Base;
        if (Digit < 10)
            Result[--ResPos] = '0' + Digit;
        else
            Result[--ResPos] = 'a' + Digit - 10;
        
        Num = NextNum;
    }
    
    if (FieldWidth > 0 && !LeftJustify)
        PrintRepeatedChar(ZeroPad ? '0' : ' ', FieldWidth - (sizeof(Result) - 1 - ResPos), Stream);
        
    PrintStr(&Result[ResPos], Stream);

    if (FieldWidth > 0 && LeftJustify)
        PrintRepeatedChar(' ', FieldWidth - (sizeof(Result) - 1 - ResPos), Stream);
}

/* print an integer to a stream without using printf/sprintf */
void PrintSimpleInt(long Num, struct OutputStream *Stream)
{
    PrintInt(Num, -1, FALSE, FALSE, Stream);
}

/* print an integer to a stream without using printf/sprintf */
void PrintInt(long Num, int FieldWidth, int ZeroPad, int LeftJustify, struct OutputStream *Stream)
{
    if (Num < 0)
    {
        PrintCh('-', Stream);
        Num = -Num;
        if (FieldWidth != 0)
            FieldWidth--;
    }
    
    PrintUnsigned((unsigned long)Num, 10, FieldWidth, ZeroPad, LeftJustify, Stream);
}

/* print a double to a stream without using printf/sprintf */
void PrintFP(double Num, struct OutputStream *Stream)
{
    int Exponent = 0;
    int MaxDecimal;
    
    if (Num < 0)
    {
        PrintCh('-', Stream);
        Num = -Num;    
    }
    
    if (Num >= 1e7)
        Exponent = log10(Num);
    else if (Num <= 1e-7 && Num != 0.0)
        Exponent = log10(Num) - 0.999999999;
    
    Num /= pow(10.0, Exponent);    
    PrintInt((long)Num, 0, FALSE, FALSE, Stream);
    PrintCh('.', Stream);
    Num = (Num - (long)Num) * 10;
    if (abs(Num) >= 1e-7)
    {
        for (MaxDecimal = 6; MaxDecimal > 0 && abs(Num) >= 1e-7; Num = (Num - (long)(Num + 1e-7)) * 10, MaxDecimal--)
            PrintCh('0' + (long)(Num + 1e-7), Stream);
    }
    else
        PrintCh('0', Stream);
        
    if (Exponent != 0)
    {
        PrintCh('e', Stream);
        PrintInt(Exponent, 0, FALSE, FALSE, Stream);
    }
}

/* intrinsic functions made available to the language */
void GenericPrintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs, struct OutputStream *Stream)
{
    char *FPos;
    Value *NextArg = Param[0];
    struct ValueType *FormatType;
    int ArgCount = 1;
    int LeftJustify = FALSE;
    int ZeroPad = FALSE;
    int FieldWidth = 0;
    char *Format = Param[0]->Val->Pointer;
    
    for (FPos = Format; *FPos != '\0'; FPos++)
    {
        if (*FPos == '%')
        {
            FPos++;
	    FieldWidth = 0;
            if (*FPos == '-')
            {
                /* a leading '-' means left justify */
                LeftJustify = TRUE;
                FPos++;
            }
            
            if (*FPos == '0')
            {
                /* a leading zero means zero pad a decimal number */
                ZeroPad = TRUE;
                FPos++;
            }
            
            /* get any field width in the format */
            while (isdigit((int)*FPos))
                FieldWidth = FieldWidth * 10 + (*FPos++ - '0');
            
            /* now check the format type */
            switch (*FPos)
            {
                case 's': FormatType = CharPtrType; break;
                case 'd': case 'u': case 'x': case 'b': case 'c': FormatType = &IntType; break;
                case 'f': FormatType = &DoubleType; break;
                case '%': PrintCh('%', Stream); FormatType = nullptr; break;
                case '\0': FPos--; FormatType = nullptr; break;
                default:  PrintCh(*FPos, Stream); FormatType = nullptr; break;
            }
            
            if (FormatType != nullptr)
            { 
                /* we have to format something */
                if (ArgCount >= NumArgs)
                    PrintStr("XXX", Stream);   /* not enough parameters for format */
                else
                {
                    NextArg = (Value *)((char *)NextArg + MEM_ALIGN(sizeof(Value) + TypeStackSizeValue(NextArg)));
                    if (NextArg->Typ != FormatType && 
                            !((FormatType == &IntType || *FPos == 'f') && IS_NUMERIC_COERCIBLE(NextArg)) &&
                            !(FormatType == CharPtrType && (NextArg->Typ->Base == BaseType::TypePointer || 
                                                             (NextArg->Typ->Base == BaseType::TypeArray && NextArg->Typ->FromType->Base == BaseType::TypeChar) ) ) )
                        PrintStr("XXX", Stream);   /* bad type for format */
                    else
                    {
                        switch (*FPos)
                        {
                            case 's':
                            {
                                char *Str;
                                
                                if (NextArg->Typ->Base == BaseType::TypePointer)
                                    Str = NextArg->Val->Pointer;
                                else
                                    Str = &NextArg->Val->ArrayMem[0];
                                    
                                if (Str == nullptr)
                                    PrintStr("nullptr", Stream);
                                else
                                    PrintStr(Str, Stream); 
                                break;
                            }
                            case 'd': PrintInt(ExpressionCoerceInteger(NextArg), FieldWidth, ZeroPad, LeftJustify, Stream); break;
                            case 'u': PrintUnsigned(ExpressionCoerceUnsignedInteger(NextArg), 10, FieldWidth, ZeroPad, LeftJustify, Stream); break;
                            case 'x': PrintUnsigned(ExpressionCoerceUnsignedInteger(NextArg), 16, FieldWidth, ZeroPad, LeftJustify, Stream); break;
                            case 'b': PrintUnsigned(ExpressionCoerceUnsignedInteger(NextArg), 2, FieldWidth, ZeroPad, LeftJustify, Stream); break;
                            case 'c': PrintCh(ExpressionCoerceUnsignedInteger(NextArg), Stream); break;
                            case 'f': PrintFP(ExpressionCoerceFP(NextArg), Stream); break;
                        }
                    }
                }
                
                ArgCount++;
            }
        }
        else
            PrintCh(*FPos, Stream);
    }
}

/* printf(): print to console output */
void LibPrintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    struct OutputStream ConsoleStream;
    
    ConsoleStream.Putch = &PlatformPutc;
    GenericPrintf(Parser, ReturnValue, Param, NumArgs, &ConsoleStream);
}

/* sprintf(): print to a string */
void LibSPrintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    struct OutputStream StrStream;
    
    StrStream.Putch = &SPutc;
    StrStream.i.Str.Parser = Parser;
    StrStream.i.Str.WritePos = Param[0]->Val->Pointer;

    GenericPrintf(Parser, ReturnValue, Param+1, NumArgs-1, &StrStream);
    PrintCh(0, &StrStream);
    ReturnValue->Val->Pointer = *Param;
}

/* get a line of input. protected from buffer overrun */
void LibGets(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = PlatformGetLine(Param[0]->Val->Pointer, GETS_BUF_MAX, nullptr);
    if (ReturnValue->Val->Pointer != nullptr)
    {
        char *EOLPos = strchr(Param[0]->Val->Pointer, '\n');
        if (EOLPos != nullptr)
            *EOLPos = '\0';
    }
}

void LibGetc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = PlatformGetCharacter();
}

void LibExit(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    PlatformExit(Param[0]->Val->Integer);
}

#ifdef PICOC_LIBRARY
void LibSin(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = sin(Param[0]->Val->Double);
}

void LibCos(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = cos(Param[0]->Val->Double);
}

void LibTan(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = tan(Param[0]->Val->Double);
}

void LibAsin(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = asin(Param[0]->Val->Double);
}

void LibAcos(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = acos(Param[0]->Val->Double);
}

void LibAtan(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = atan(Param[0]->Val->Double);
}

void LibSinh(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = sinh(Param[0]->Val->Double);
}

void LibCosh(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = cosh(Param[0]->Val->Double);
}

void LibTanh(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = tanh(Param[0]->Val->Double);
}

void LibExp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = exp(Param[0]->Val->Double);
}

void LibFabs(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = fabs(Param[0]->Val->Double);
}

void LibLog(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = log(Param[0]->Val->Double);
}

void LibLog10(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = log10(Param[0]->Val->Double);
}

void LibPow(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = pow(Param[0]->Val->Double, Param[1]->Val->Double);
}

void LibSqrt(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = sqrt(Param[0]->Val->Double);
}

void LibRound(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = floor(Param[0]->Val->Double + 0.5);   /* XXX - fix for soft float */
}

void LibCeil(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = ceil(Param[0]->Val->Double);
}

void LibFloor(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Double = floor(Param[0]->Val->Double);
}
#endif

#ifndef NO_STRING_FUNCTIONS
void LibMalloc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = malloc(Param[0]->Val->Integer);
}

#ifndef NO_CALLOC
void LibCalloc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = calloc(Param[0]->Val->Integer, Param[1]->Val->Integer);
}
#endif

#ifndef NO_REALLOC
void LibRealloc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = realloc(Param[0]->Val->Pointer, Param[1]->Val->Integer);
}
#endif

void LibFree(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    free(Param[0]->Val->Pointer);
}

void LibStrcpy(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    char *To = (char *)Param[0]->Val->Pointer;
    char *From = (char *)Param[1]->Val->Pointer;
    
    while (*From != '\0')
        *To++ = *From++;
    
    *To = '\0';
}

void LibStrncpy(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    char *To = (char *)Param[0]->Val->Pointer;
    char *From = (char *)Param[1]->Val->Pointer;
    int Len = Param[2]->Val->Integer;
    
    for (; *From != '\0' && Len > 0; Len--)
        *To++ = *From++;
    
    if (Len > 0)
        *To = '\0';
}

void LibStrcmp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    char *Str1 = (char *)Param[0]->Val->Pointer;
    char *Str2 = (char *)Param[1]->Val->Pointer;
    int StrEnded;
    
    for (StrEnded = FALSE; !StrEnded; StrEnded = (*Str1 == '\0' || *Str2 == '\0'), Str1++, Str2++)
    {
         if (*Str1 < *Str2) { ReturnValue->Val->Integer = -1; return; } 
         else if (*Str1 > *Str2) { ReturnValue->Val->Integer = 1; return; }
    }
    
    ReturnValue->Val->Integer = 0;
}

void LibStrncmp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    char *Str1 = (char *)Param[0]->Val->Pointer;
    char *Str2 = (char *)Param[1]->Val->Pointer;
    int Len = Param[2]->Val->Integer;
    int StrEnded;
    
    for (StrEnded = FALSE; !StrEnded && Len > 0; StrEnded = (*Str1 == '\0' || *Str2 == '\0'), Str1++, Str2++, Len--)
    {
         if (*Str1 < *Str2) { ReturnValue->Val->Integer = -1; return; } 
         else if (*Str1 > *Str2) { ReturnValue->Val->Integer = 1; return; }
    }
    
    ReturnValue->Val->Integer = 0;
}

void LibStrcat(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    char *To = (char *)Param[0]->Val->Pointer;
    char *From = (char *)Param[1]->Val->Pointer;
    
    while (*To != '\0')
        To++;
    
    while (*From != '\0')
        *To++ = *From++;
    
    *To = '\0';
}

void LibIndex(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    char *Pos = (char *)Param[0]->Val->Pointer;
    int SearchChar = Param[1]->Val->Integer;

    while (*Pos != '\0' && *Pos != SearchChar)
        Pos++;
    
    if (*Pos != SearchChar)
        ReturnValue->Val->Pointer = nullptr;
    else
        ReturnValue->Val->Pointer = Pos;
}

void LibRindex(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    char *Pos = (char *)Param[0]->Val->Pointer;
    int SearchChar = Param[1]->Val->Integer;

    ReturnValue->Val->Pointer = nullptr;
    for (; *Pos != '\0'; Pos++)
    {
        if (*Pos == SearchChar)
            ReturnValue->Val->Pointer = Pos;
    }
}

void LibStrlen(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    char *Pos = (char *)Param[0]->Val->Pointer;
    int Len;
    
    for (Len = 0; *Pos != '\0'; Pos++)
        Len++;
    
    ReturnValue->Val->Integer = Len;
}

void LibMemset(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    /* we can use the system memset() */
    memset(Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void LibMemcpy(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    /* we can use the system memcpy() */
    memcpy(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void LibMemcmp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    unsigned char *Mem1 = (unsigned char *)Param[0]->Val->Pointer;
    unsigned char *Mem2 = (unsigned char *)Param[1]->Val->Pointer;
    int Len = Param[2]->Val->Integer;
    
    for (; Len > 0; Mem1++, Mem2++, Len--)
    {
         if (*Mem1 < *Mem2) { ReturnValue->Val->Integer = -1; return; } 
         else if (*Mem1 > *Mem2) { ReturnValue->Val->Integer = 1; return; }
    }
    
    ReturnValue->Val->Integer = 0;
}
#endif

/* list of all library functions and their prototypes */
struct LibraryFunction CLibrary[] =
{
    { LibPrintf,        "void printf(char *, ...);" },
    { LibSPrintf,       "char *sprintf(char *, char *, ...);" },
    { LibGets,          "char *gets(char *);" },
    { LibGetc,          "int getchar();" },
    { LibExit,          "void exit(int);" },
#ifdef PICOC_LIBRARY
    { LibSin,           "float sin(float);" },
    { LibCos,           "float cos(float);" },
    { LibTan,           "float tan(float);" },
    { LibAsin,          "float asin(float);" },
    { LibAcos,          "float acos(float);" },
    { LibAtan,          "float atan(float);" },
    { LibSinh,          "float sinh(float);" },
    { LibCosh,          "float cosh(float);" },
    { LibTanh,          "float tanh(float);" },
    { LibExp,           "float exp(float);" },
    { LibFabs,          "float fabs(float);" },
    { LibLog,           "float log(float);" },
    { LibLog10,         "float log10(float);" },
    { LibPow,           "float pow(float,float);" },
    { LibSqrt,          "float sqrt(float);" },
    { LibRound,         "float round(float);" },
    { LibCeil,          "float ceil(float);" },
    { LibFloor,         "float floor(float);" },
#endif
    { LibMalloc,        "void *malloc(int);" },
#ifndef NO_CALLOC
    { LibCalloc,        "void *calloc(int,int);" },
#endif
#ifndef NO_REALLOC
    { LibRealloc,       "void *realloc(void *,int);" },
#endif
    { LibFree,          "void free(void *);" },
#ifndef NO_STRING_FUNCTIONS
    { LibStrcpy,        "void strcpy(char *,char *);" },
    { LibStrncpy,       "void strncpy(char *,char *,int);" },
    { LibStrcmp,        "int strcmp(char *,char *);" },
    { LibStrncmp,       "int strncmp(char *,char *,int);" },
    { LibStrcat,        "void strcat(char *,char *);" },
    { LibIndex,         "char *index(char *,int);" },
    { LibRindex,        "char *rindex(char *,int);" },
    { LibStrlen,        "int strlen(char *);" },
    { LibMemset,        "void memset(void *,int,int);" },
    { LibMemcpy,        "void memcpy(void *,void *,int);" },
    { LibMemcmp,        "int memcmp(void *,void *,int);" },
#endif
    { nullptr,             nullptr }
};

#endif /* BUILTIN_MINI_STDLIB */
