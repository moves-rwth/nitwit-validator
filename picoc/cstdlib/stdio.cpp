/* stdio.h library for large systems - small embedded systems use clibrary.c instead */
#ifndef BUILTIN_MINI_STDLIB

#include <errno.h>
#include "../interpreter.hpp"

#include "../CoerceT.hpp"

#define MAX_FORMAT 80
#define MAX_SCANF_ARGS 10

static int Stdio_ZeroValue = 0;
static int EOFValue = EOF;
static int SEEK_SETValue = SEEK_SET;
static int SEEK_CURValue = SEEK_CUR;
static int SEEK_ENDValue = SEEK_END;
static int BUFSIZValue = BUFSIZ;
static int FILENAME_MAXValue = FILENAME_MAX;
static int _IOFBFValue = _IOFBF;
static int _IOLBFValue = _IOLBF;
static int _IONBFValue = _IONBF;
static int L_tmpnamValue = L_tmpnam;
static int GETS_MAXValue = 255;     /* arbitrary maximum size of a gets() file */

static FILE *stdinValue;
static FILE *stdoutValue;
static FILE *stderrValue;


/* our own internal output stream which can output to FILE * or strings */
typedef struct StdOutStreamStruct
{
    FILE *FilePtr;
    char *StrOutPtr;
    int StrOutLen;
    int CharCount;
    
} StdOutStream;

/* our representation of varargs within picoc */
struct StdVararg
{
    Value **Param;
    int NumArgs;
};

/* initialises the I/O system so error reporting works */
void BasicIOInit(Picoc *pc)
{
    pc->CStdOut = stdout;
    stdinValue = stdin;
    stdoutValue = stdout;
    stderrValue = stderr;
}

/* output a single character to either a FILE * or a string */
void StdioOutPutc(int OutCh, StdOutStream *Stream)
{
    if (Stream->FilePtr != nullptr)
    {
        /* output to stdio stream */
        putc(OutCh, Stream->FilePtr);
        Stream->CharCount++;
    }
    else if (Stream->StrOutLen < 0 || Stream->StrOutLen > 1)
    {
        /* output to a string */
        *Stream->StrOutPtr = OutCh;
        Stream->StrOutPtr++;
        
        if (Stream->StrOutLen > 1)
            Stream->StrOutLen--;

        Stream->CharCount++;
    }
}

/* output a string to either a FILE * or a string */
void StdioOutPuts(const char *Str, StdOutStream *Stream)
{
    if (Stream->FilePtr != nullptr)
    {
        /* output to stdio stream */
        fputs(Str, Stream->FilePtr);
    }
    else
    {
        /* output to a string */
        while (*Str != '\0')
        {
            if (Stream->StrOutLen < 0 || Stream->StrOutLen > 1)
            {
                /* output to a string */
                *Stream->StrOutPtr = *Str;
                Str++;
                Stream->StrOutPtr++;
                
                if (Stream->StrOutLen > 1)
                    Stream->StrOutLen--;
        
                Stream->CharCount++;
            }            
        }
    }
}

/* printf-style format of an int or other word-sized object */
void StdioFprintfWord(StdOutStream *Stream, const char *Format, unsigned long Value)
{
    if (Stream->FilePtr != nullptr)
        Stream->CharCount += fprintf(Stream->FilePtr, Format, Value);
    
    else if (Stream->StrOutLen >= 0)
    {
#ifndef WIN32
		int CCount = snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#else
		int CCount = _snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#endif
		Stream->StrOutPtr += CCount;
        Stream->StrOutLen -= CCount;
        Stream->CharCount += CCount;
    }
    else
    {
        int CCount = sprintf(Stream->StrOutPtr, Format, Value);
        Stream->CharCount += CCount;
        Stream->StrOutPtr += CCount;
    }
}

/* printf-style format of a floating point number */
void StdioFprintfFP(StdOutStream *Stream, const char *Format, double Value)
{
    if (Stream->FilePtr != nullptr)
        Stream->CharCount += fprintf(Stream->FilePtr, Format, Value);
    
    else if (Stream->StrOutLen >= 0)
    {
#ifndef WIN32
        int CCount = snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#else
        int CCount = _snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#endif
		Stream->StrOutPtr += CCount;
        Stream->StrOutLen -= CCount;
        Stream->CharCount += CCount;
    }
    else
    {
        int CCount = sprintf(Stream->StrOutPtr, Format, Value);
        Stream->CharCount += CCount;
        Stream->StrOutPtr += CCount;
    }
}

/* printf-style format of a pointer */
void StdioFprintfPointer(StdOutStream *Stream, const char *Format, void *Value)
{
    if (Stream->FilePtr != nullptr)
        Stream->CharCount += fprintf(Stream->FilePtr, Format, Value);
    
    else if (Stream->StrOutLen >= 0)
    {
#ifndef WIN32
        int CCount = snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#else
		int CCount = _snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#endif
        Stream->StrOutPtr += CCount;
        Stream->StrOutLen -= CCount;
        Stream->CharCount += CCount;
    }
    else
    {
        int CCount = sprintf(Stream->StrOutPtr, Format, Value);
        Stream->CharCount += CCount;
        Stream->StrOutPtr += CCount;
    }
}

/* internal do-anything v[s][n]printf() formatting system with output to strings or FILE * */
int StdioBasePrintf(struct ParseState *Parser, FILE *Stream, char *StrOut, int StrOutLen, char *Format, struct StdVararg *Args)
{
    Value *ThisArg = Args->Param[0];
    int ArgCount = 0;
    char *FPos;
    char OneFormatBuf[MAX_FORMAT+1];
    int OneFormatCount;
    struct ValueType *ShowType;
    StdOutStream SOStream;
    Picoc *pc = Parser->pc;
    
    if (Format == nullptr)
        Format = "[null format]\n";
    
    FPos = Format;    
    SOStream.FilePtr = Stream;
    SOStream.StrOutPtr = StrOut;
    SOStream.StrOutLen = StrOutLen;
    SOStream.CharCount = 0;
    
    while (*FPos != '\0')
    {
        if (*FPos == '%')
        {
            /* work out what type we're printing */
            FPos++;
            ShowType = nullptr;
            OneFormatBuf[0] = '%';
            OneFormatCount = 1;
            
            do
            {
                switch (*FPos)
                {
                    case 'd': case 'i':     ShowType = &pc->IntType; break;     /* integer decimal */
                    case 'o': case 'u': case 'x': case 'X': ShowType = &pc->IntType; break; /* integer base conversions */
                    case 'e': case 'E':     ShowType = &pc->DoubleType; break;      /* double, exponent form */
                    case 'f': case 'F':     ShowType = &pc->DoubleType; break;      /* double, fixed-point */
                    case 'g': case 'G':     ShowType = &pc->DoubleType; break;      /* double, flexible format */
                    case 'a': case 'A':     ShowType = &pc->IntType; break;     /* hexadecimal, 0x- format */
                    case 'c':               ShowType = &pc->IntType; break;     /* character */
                    case 's':               ShowType = pc->CharPtrType; break;  /* string */
                    case 'p':               ShowType = pc->VoidPtrType; break;  /* pointer */
                    case 'n':               ShowType = &pc->VoidType; break;    /* number of characters written */
                    case 'm':               ShowType = &pc->VoidType; break;    /* strerror(errno) */
                    case '%':               ShowType = &pc->VoidType; break;    /* just a '%' character */
                    case '\0':              ShowType = &pc->VoidType; break;    /* end of format string */
                }
                
                /* copy one character of format across to the OneFormatBuf */
                OneFormatBuf[OneFormatCount] = *FPos;
                OneFormatCount++;

                /* do special actions depending on the conversion type */
                if (ShowType == &pc->VoidType)
                {
                    switch (*FPos)
                    {
                        case 'm':   StdioOutPuts(strerror(errno), &SOStream); break;
                        case '%':   StdioOutPutc(*FPos, &SOStream); break;
                        case '\0':  OneFormatBuf[OneFormatCount] = '\0'; StdioOutPutc(*FPos, &SOStream); break;
                        case 'n':   
                            ThisArg = (Value *)((char *)ThisArg + MEM_ALIGN(sizeof(Value) + TypeStackSizeValue(ThisArg)));
                            if (ThisArg->Typ->Base == BaseType::TypeArray && ThisArg->Typ->FromType->Base == BaseType::TypeInt)
                                *(int *)ThisArg->Val->Pointer = SOStream.CharCount;
                            break;
                    }
                }
                
                FPos++;
                
            } while (ShowType == nullptr && OneFormatCount < MAX_FORMAT);
            
            if (ShowType != &pc->VoidType)
            {
                if (ArgCount >= Args->NumArgs)
                    StdioOutPuts("XXX", &SOStream);
                else
                {
                    /* null-terminate the buffer */
                    OneFormatBuf[OneFormatCount] = '\0';
    
                    /* print this argument */
                    ThisArg = (Value *)((char *)ThisArg + MEM_ALIGN(sizeof(Value) + TypeStackSizeValue(ThisArg)));
                    if (ShowType == &pc->IntType)
                    {
                        /* show a signed integer */
                        if (IS_NUMERIC_COERCIBLE(ThisArg))
                            StdioFprintfWord(&SOStream, OneFormatBuf, CoerceT<unsigned long long>(ThisArg));
                        else
                            StdioOutPuts("XXX", &SOStream);
                    }
                    else if (ShowType == &pc->DoubleType)
                    {
                        /* show a floating point number */
                        if (IS_NUMERIC_COERCIBLE(ThisArg))
                            StdioFprintfFP(&SOStream, OneFormatBuf, CoerceT<double>(ThisArg));
                        else
                            StdioOutPuts("XXX", &SOStream);
                    }
                    else if (ShowType == &pc->FloatType)
                    {
                        /* show a floating point number */
                        if (IS_NUMERIC_COERCIBLE(ThisArg))
                            StdioFprintfFP(&SOStream, OneFormatBuf, CoerceT<float>(ThisArg));
                        else
                            StdioOutPuts("XXX", &SOStream);
                    }
                    else if (ShowType == pc->CharPtrType)
                    {
                        if (ThisArg->Typ->Base == BaseType::TypePointer)
                            StdioFprintfPointer(&SOStream, OneFormatBuf, ThisArg->Val->Pointer);
                            
                        else if (ThisArg->Typ->Base == BaseType::TypeArray && ThisArg->Typ->FromType->Base == BaseType::TypeChar)
                            StdioFprintfPointer(&SOStream, OneFormatBuf, &ThisArg->Val->ArrayMem[0]);
                            
                        else
                            StdioOutPuts("XXX", &SOStream);
                    }
                    else if (ShowType == pc->VoidPtrType)
                    {
                        if (ThisArg->Typ->Base == BaseType::TypePointer)
                            StdioFprintfPointer(&SOStream, OneFormatBuf, ThisArg->Val->Pointer);
                            
                        else if (ThisArg->Typ->Base == BaseType::TypeArray)
                            StdioFprintfPointer(&SOStream, OneFormatBuf, &ThisArg->Val->ArrayMem[0]);
                            
                        else
                            StdioOutPuts("XXX", &SOStream);
                    }
                    
                    ArgCount++;
                }
            }
        }
        else
        {
            /* just output a normal character */
            StdioOutPutc(*FPos, &SOStream);
            FPos++;
        }
    }
    
    /* null-terminate */
    if (SOStream.StrOutPtr != nullptr && SOStream.StrOutLen > 0)
        *SOStream.StrOutPtr = '\0';      
    
    return SOStream.CharCount;
}

/* internal do-anything v[s][n]scanf() formatting system with input from strings or FILE * */
int StdioBaseScanf(struct ParseState *Parser, FILE *Stream, char *StrIn, char *Format, struct StdVararg *Args)
{
    Value *ThisArg = Args->Param[0];
    int ArgCount = 0;
    void *ScanfArg[MAX_SCANF_ARGS];
    
    if (Args->NumArgs > MAX_SCANF_ARGS)
        ProgramFail(Parser, "too many arguments to scanf() - %d max", MAX_SCANF_ARGS);
    
    for (ArgCount = 0; ArgCount < Args->NumArgs; ArgCount++)
    {
        ThisArg = (Value *)((char *)ThisArg + MEM_ALIGN(sizeof(Value) + TypeStackSizeValue(ThisArg)));
        
        if (ThisArg->Typ->Base == BaseType::TypePointer) 
            ScanfArg[ArgCount] = ThisArg->Val->Pointer;
        
        else if (ThisArg->Typ->Base == BaseType::TypeArray)
            ScanfArg[ArgCount] = &ThisArg->Val->ArrayMem[0];
        
        else
            ProgramFail(Parser, "non-pointer argument to scanf() - argument %d after format", ArgCount+1);
    }
    
    if (Stream != nullptr)
        return fscanf(Stream, Format, ScanfArg[0], ScanfArg[1], ScanfArg[2], ScanfArg[3], ScanfArg[4], ScanfArg[5], ScanfArg[6], ScanfArg[7], ScanfArg[8], ScanfArg[9]);
    else
        return sscanf(StrIn, Format, ScanfArg[0], ScanfArg[1], ScanfArg[2], ScanfArg[3], ScanfArg[4], ScanfArg[5], ScanfArg[6], ScanfArg[7], ScanfArg[8], ScanfArg[9]);
}

/* stdio calls */
void StdioFopen(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = fopen((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer);
}

void StdioFreopen(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = freopen((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer, (FILE*)Param[2]->Val->Pointer);
}

void StdioFclose(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fclose((FILE*)Param[0]->Val->Pointer);
}

void StdioFread(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fread(Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer,(FILE*) Param[3]->Val->Pointer);
}

void StdioFwrite(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fwrite((const void*)Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer, (FILE*)Param[3]->Val->Pointer);
}

void StdioFgetc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fgetc((FILE*)Param[0]->Val->Pointer);
}

void StdioFgets(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = fgets((char*)Param[0]->Val->Pointer, Param[1]->Val->Integer, (FILE*)Param[2]->Val->Pointer);
}

void StdioRemove(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = remove((const char*)Param[0]->Val->Pointer);
}

void StdioRename(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = rename((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer);
}

void StdioRewind(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    rewind((FILE*)Param[0]->Val->Pointer);
}

void StdioTmpfile(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = tmpfile();
}

void StdioClearerr(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    clearerr((FILE *)Param[0]->Val->Pointer);
}

void StdioFeof(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = feof((FILE *)Param[0]->Val->Pointer);
}

void StdioFerror(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = ferror((FILE *)Param[0]->Val->Pointer);
}

void StdioFileno(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fileno((FILE*)Param[0]->Val->Pointer);
}

void StdioFflush(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fflush((FILE*)Param[0]->Val->Pointer);
}

void StdioFgetpos(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fgetpos((FILE*)Param[0]->Val->Pointer, (fpos_t*)Param[1]->Val->Pointer);
}

void StdioFsetpos(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fsetpos((FILE*)Param[0]->Val->Pointer, (const fpos_t*)Param[1]->Val->Pointer);
}

void StdioFputc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fputc(Param[0]->Val->Integer, (FILE*)Param[1]->Val->Pointer);
}

void StdioFputs(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fputs((const char*)Param[0]->Val->Pointer, (FILE*)Param[1]->Val->Pointer);
}

void StdioFtell(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = ftell((FILE*)Param[0]->Val->Pointer);
}

void StdioFseek(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fseek((FILE*)Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void StdioPerror(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    perror((const char*)Param[0]->Val->Pointer);
}

void StdioPutc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = putc(Param[0]->Val->Integer, (FILE*)Param[1]->Val->Pointer);
}

void StdioPutchar(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = putchar(Param[0]->Val->Integer);
}

void StdioSetbuf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    setbuf((FILE*)Param[0]->Val->Pointer, (char*)Param[1]->Val->Pointer);
}

void StdioSetvbuf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    setvbuf((FILE*)Param[0]->Val->Pointer, (char*)Param[1]->Val->Pointer, Param[2]->Val->Integer, Param[3]->Val->Integer);
}

void StdioUngetc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = ungetc(Param[0]->Val->Integer, (FILE*)Param[1]->Val->Pointer);
}

void StdioPuts(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = puts((const char*)Param[0]->Val->Pointer);
}

void StdioGets(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = fgets((char*)Param[0]->Val->Pointer, GETS_MAXValue, stdin);
    if (ReturnValue->Val->Pointer != nullptr)
    {
        char *EOLPos = strchr((char*)Param[0]->Val->Pointer, '\n');
        if (EOLPos != nullptr)
            *EOLPos = '\0';
    }
}

void StdioGetchar(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getchar();
}

void StdioPrintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    struct StdVararg PrintfArgs;
    
    PrintfArgs.Param = Param;
    PrintfArgs.NumArgs = NumArgs-1;
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, stdout, nullptr, 0, (char*)Param[0]->Val->Pointer, &PrintfArgs);
}

void StdioVprintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, stdout, nullptr, 0, (char*)Param[0]->Val->Pointer, (StdVararg*)Param[1]->Val->Pointer);
}

void StdioFprintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    struct StdVararg PrintfArgs;
    
    PrintfArgs.Param = Param + 1;
    PrintfArgs.NumArgs = NumArgs-2;
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, (FILE*)Param[0]->Val->Pointer, nullptr, 0, (char*)Param[1]->Val->Pointer, &PrintfArgs);
}

void StdioVfprintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, (FILE*)Param[0]->Val->Pointer, nullptr, 0, (char*)Param[1]->Val->Pointer, (StdVararg*)Param[2]->Val->Pointer);
}

void StdioSprintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    struct StdVararg PrintfArgs;
    
    PrintfArgs.Param = Param + 1;
    PrintfArgs.NumArgs = NumArgs-2;
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, nullptr, (char*) Param[0]->Val->Pointer, -1, (char*)Param[1]->Val->Pointer, &PrintfArgs);
}

void StdioSnprintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    struct StdVararg PrintfArgs;
    
    PrintfArgs.Param = Param+2;
    PrintfArgs.NumArgs = NumArgs-3;
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, nullptr, (char*)Param[0]->Val->Pointer, Param[1]->Val->Integer, (char*)Param[2]->Val->Pointer, &PrintfArgs);
}

void StdioScanf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    struct StdVararg ScanfArgs;
    
    ScanfArgs.Param = Param;
    ScanfArgs.NumArgs = NumArgs-1;
    ReturnValue->Val->Integer = StdioBaseScanf(Parser, stdin, nullptr, (char*)Param[0]->Val->Pointer, &ScanfArgs);
}

void StdioFscanf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    struct StdVararg ScanfArgs;
    
    ScanfArgs.Param = Param+1;
    ScanfArgs.NumArgs = NumArgs-2;
    ReturnValue->Val->Integer = StdioBaseScanf(Parser, (FILE*)Param[0]->Val->Pointer, nullptr, (char*)Param[1]->Val->Pointer, &ScanfArgs);
}

void StdioSscanf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    struct StdVararg ScanfArgs;
    
    ScanfArgs.Param = Param+1;
    ScanfArgs.NumArgs = NumArgs-2;
    ReturnValue->Val->Integer = StdioBaseScanf(Parser, nullptr, (char*)Param[0]->Val->Pointer, (char*)Param[1]->Val->Pointer, &ScanfArgs);
}

void StdioVsprintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, nullptr, (char*)Param[0]->Val->Pointer, -1, (char*)Param[1]->Val->Pointer, (StdVararg*)Param[2]->Val->Pointer);
}

void StdioVsnprintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, nullptr,(char*) Param[0]->Val->Pointer, Param[1]->Val->Integer, (char*)Param[2]->Val->Pointer, (StdVararg*)Param[3]->Val->Pointer);
}

void StdioVscanf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBaseScanf(Parser, stdin, nullptr, (char*)Param[0]->Val->Pointer, (StdVararg*)Param[1]->Val->Pointer);
}

void StdioVfscanf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBaseScanf(Parser, (FILE*)Param[0]->Val->Pointer, nullptr, (char*)Param[1]->Val->Pointer, (StdVararg*)Param[2]->Val->Pointer);
}

void StdioVsscanf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBaseScanf(Parser, nullptr, (char*)Param[0]->Val->Pointer, (char*)Param[1]->Val->Pointer, (StdVararg*)Param[2]->Val->Pointer);
}

void NoOp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs){

}
/* handy structure definitions */
const char StdioDefs[] = "\
typedef struct __va_listStruct va_list; \
typedef struct __FILEStruct FILE;\
";

/* all stdio functions */
struct LibraryFunction StdioFunctions[] =
{
    { StdioFopen,   "FILE *fopen(char *, char *);" },
    { StdioFreopen, "FILE *freopen(char *, char *, FILE *);" },
    { StdioFclose,  "int fclose(FILE *);" },
    { StdioFread,   "int fread(void *, int, int, FILE *);" },
    { StdioFwrite,  "int fwrite(void *, int, int, FILE *);" },
    { StdioFgetc,   "int fgetc(FILE *);" },
    { StdioFgetc,   "int getc(FILE *);" },
    { StdioFgets,   "char *fgets(char *, int, FILE *);" },
    { StdioFputc,   "int fputc(int, FILE *);" },
    { StdioFputs,   "int fputs(char *, FILE *);" },
    { StdioRemove,  "int remove(char *);" },
    { StdioRename,  "int rename(char *, char *);" },
    { StdioRewind,  "void rewind(FILE *);" },
    { StdioTmpfile, "FILE *tmpfile();" },
    { StdioClearerr,"void clearerr(FILE *);" },
    { StdioFeof,    "int feof(FILE *);" },
    { StdioFerror,  "int ferror(FILE *);" },
    { StdioFileno,  "int fileno(FILE *);" },
    { StdioFflush,  "int fflush(FILE *);" },
    { StdioFgetpos, "int fgetpos(FILE *, int *);" },
    { StdioFsetpos, "int fsetpos(FILE *, int *);" },
    { StdioFtell,   "int ftell(FILE *);" },
    { StdioFseek,   "int fseek(FILE *, int, int);" },
#ifdef VERBOSE
    { StdioPerror,  "void perror(char *);" },
    { StdioPutc,    "int putc(char *, FILE *);" },
    { StdioPutchar, "int putchar(int);" },
    { StdioPutchar, "int fputchar(int);" },
#else
    { NoOp,  "void perror(char *);" },
    { NoOp,    "int putc(char *, FILE *);" },
    { NoOp, "int putchar(int);" },
    { NoOp, "int fputchar(int);" },
#endif
    { StdioSetbuf,  "void setbuf(FILE *, char *);" },
    { StdioSetvbuf, "void setvbuf(FILE *, char *, int, int);" },
    { StdioUngetc,  "int ungetc(int, FILE *);" },
#ifdef VERBOSE
    { StdioPuts,    "int puts(char *);" },
#else
    { NoOp,    "int puts(char *);" },
#endif
    { StdioGets,    "char *gets(char *);" },
    { StdioGetchar, "int getchar();" },
#ifdef VERBOSE
    { StdioPrintf,  "int printf(char *, ...);" },
    { StdioFprintf, "int fprintf(FILE *, char *, ...);" },
    { StdioSprintf, "int sprintf(char *, char *, ...);" },
    { StdioSnprintf,"int snprintf(char *, int, char *, ...);" },
#else
    { NoOp,  "int printf(char *, ...);" },
    { NoOp, "int fprintf(FILE *, char *, ...);" },
    { NoOp, "int sprintf(char *, char *, ...);" },
    { NoOp,"int snprintf(char *, int, char *, ...);" },
#endif
    { StdioScanf,   "int scanf(char *, ...);" },
    { StdioFscanf,  "int fscanf(FILE *, char *, ...);" },
    { StdioSscanf,  "int sscanf(char *, char *, ...);" },
#ifdef VERBOSE
    { StdioVprintf, "int vprintf(char *, va_list);" },
    { StdioVfprintf,"int vfprintf(FILE *, char *, va_list);" },
    { StdioVsprintf,"int vsprintf(char *, char *, va_list);" },
    { StdioVsnprintf,"int vsnprintf(char *, int, char *, va_list);" },
#else
    { NoOp, "int vprintf(char *, va_list);" },
    { NoOp,"int vfprintf(FILE *, char *, va_list);" },
    { NoOp,"int vsprintf(char *, char *, va_list);" },
    { NoOp,"int vsnprintf(char *, int, char *, va_list);" },
#endif
    { StdioVscanf,   "int vscanf(char *, va_list);" },
    { StdioVfscanf,  "int vfscanf(FILE *, char *, va_list);" },
    { StdioVsscanf,  "int vsscanf(char *, char *, va_list);" },


    { nullptr,         nullptr }
};

/* creates various system-dependent definitions */
void StdioSetupFunc(Picoc *pc)
{
    struct ValueType *StructFileType;
    struct ValueType *FilePtrType;

    /* make a "struct __FILEStruct" which is the same size as a native FILE structure */
    StructFileType = TypeCreateOpaqueStruct(pc, nullptr, nitwit::table::TableStrRegister(pc, "__FILEStruct"), sizeof(FILE));
    
    /* get a FILE * type */
    FilePtrType = TypeGetMatching(pc, nullptr, StructFileType, BaseType::TypePointer, 0, pc->StrEmpty, TRUE, nullptr);

    /* make a "struct __va_listStruct" which is the same size as our struct StdVararg */
    TypeCreateOpaqueStruct(pc, nullptr, nitwit::table::TableStrRegister(pc, "__va_listStruct"), sizeof(FILE));
    
    /* define EOF equal to the system EOF */
    VariableDefinePlatformVar(pc, nullptr, "EOF", &pc->IntType, (union AnyValue *)&EOFValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "SEEK_SET", &pc->IntType, (union AnyValue *)&SEEK_SETValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "SEEK_CUR", &pc->IntType, (union AnyValue *)&SEEK_CURValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "SEEK_END", &pc->IntType, (union AnyValue *)&SEEK_ENDValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "BUFSIZ", &pc->IntType, (union AnyValue *)&BUFSIZValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "FILENAME_MAX", &pc->IntType, (union AnyValue *)&FILENAME_MAXValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "_IOFBF", &pc->IntType, (union AnyValue *)&_IOFBFValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "_IOLBF", &pc->IntType, (union AnyValue *)&_IOLBFValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "_IONBF", &pc->IntType, (union AnyValue *)&_IONBFValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "L_tmpnam", &pc->IntType, (union AnyValue *)&L_tmpnamValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "GETS_MAX", &pc->IntType, (union AnyValue *)&GETS_MAXValue, FALSE);
    
    /* define stdin, stdout and stderr */
    VariableDefinePlatformVar(pc, nullptr, "stdin", FilePtrType, (union AnyValue *)&stdinValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "stdout", FilePtrType, (union AnyValue *)&stdoutValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "stderr", FilePtrType, (union AnyValue *)&stderrValue, FALSE);

    /* define NULL, TRUE and FALSE */
    if (!VariableDefined(pc, nitwit::table::TableStrRegister(pc, "NULL")))
        VariableDefinePlatformVar(pc, nullptr, "NULL", &pc->IntType, (union AnyValue *)&Stdio_ZeroValue, FALSE);
}

/* portability-related I/O calls */
void PrintCh(char OutCh, FILE *Stream)
{
    putc(OutCh, Stream);
}

void PrintSimpleInt(long Num, FILE *Stream)
{
    fprintf(Stream, "%ld", Num);
}

void PrintStr(const char *Str, FILE *Stream)
{
    fputs(Str, Stream);
}

void PrintFP(double Num, FILE *Stream)
{
    fprintf(Stream, "%f", Num);
}

#endif /* !BUILTIN_MINI_STDLIB */
