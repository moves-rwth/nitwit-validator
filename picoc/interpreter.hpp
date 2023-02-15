/* picoc main header file - this has all the main data structures and 
 * function prototypes. If you're just calling picoc you should look at the
 * external interface instead, in picoc.h */
 
#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "platform.hpp"

/* handy definitions */
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif


#ifndef min
#define min(x,y) (((x)<(y))?(x):(y))
#endif

#define MEM_ALIGN(x) (((x) + sizeof(ALIGN_TYPE) - 1) & ~(sizeof(ALIGN_TYPE)-1))

#define GETS_BUF_MAX 512

/* for debugging */
#define PRINT_SOURCE_POS ({ PrintSourceTextErrorLine(Parser->pc->CStdOut, Parser->FileName, Parser->SourceText, Parser->Line, Parser->CharacterPos); PlatformPrintf(Parser->pc->CStdOut, "\n"); })
#define PRINT_TYPE(typ) PlatformPrintf(Parser->pc->CStdOut, "%t\n", typ);


/* small processors use a simplified FILE * for stdio, otherwise use the system FILE * */
#ifdef BUILTIN_MINI_STDLIB
typedef struct OutputStream IOFILE;
#else
typedef FILE IOFILE;
#endif

/* coercion of numeric types to other numeric types */
#define IS_FP(v) ((v)->Typ->Base == BaseType::TypeDouble || (v)->Typ->Base == BaseType::TypeFloat)
#define IS_FLOAT(v) ((v)->Typ->Base == BaseType::TypeFloat)
#define IS_DOUBLE(v) ((v)->Typ->Base == BaseType::TypeDouble)

#define IS_POINTER_COERCIBLE(v, ap) ((ap) ? ((v)->Typ->Base == BaseType::TypePointer) : 0)
#define POINTER_COERCE(v) ((int)(v)->Val->Pointer)

#define IS_INTEGER_NUMERIC_TYPE(t) ((t)->Base >= BaseType::TypeInt && (t)->Base <= BaseType::TypeUnsignedLongLong)
#define IS_INTEGER_NUMERIC(v) IS_INTEGER_NUMERIC_TYPE((v)->Typ)
#define IS_NUMERIC_COERCIBLE(v) (IS_INTEGER_NUMERIC(v) || IS_FP(v))
#define IS_NUMERIC_COERCIBLE_PLUS_POINTERS(v,ap) (IS_NUMERIC_COERCIBLE(v) || IS_POINTER_COERCIBLE(v,ap))
#define IS_UNSIGNED(v) (v->Typ->Base >= BaseType::TypeUnsignedInt && v->Typ->Base <= BaseType::TypeUnsignedLongLong)

#ifndef GET_VALUE
#define GET_VALUE(Value)\
    ((Value)->Typ->Base == BaseType::TypeInt ? Value->Val->Integer : (\
    (Value)->Typ->Base == BaseType::TypeChar ? Value->Val->Character : (\
    (Value)->Typ->Base == BaseType::TypeShort ? Value->Val->ShortInteger : (\
    (Value)->Typ->Base == BaseType::TypeLong ? Value->Val->LongInteger : (\
    (Value)->Typ->Base == BaseType::TypeUnsignedInt ? Value->Val->UnsignedInteger : (\
    (Value)->Typ->Base == BaseType::TypeUnsignedChar ? Value->Val->UnsignedCharacter : (\
    (Value)->Typ->Base == BaseType::TypeUnsignedShort ? Value->Val->UnsignedShortInteger : (\
    (Value)->Typ->Base == BaseType::TypeUnsignedLong ? Value->Val->UnsignedLongInteger :  Value->Val->LongInteger))))))))
#endif

#ifndef GET_LL_VALUE
#define GET_LL_VALUE(Value)\
    ((Value)->Typ->Base == BaseType::TypeLongLong ? Value->Val->LongLongInteger : (\
    (Value)->Typ->Base == BaseType::TypeUnsignedLongLong ? Value->Val->UnsignedLongLongInteger :  Value->Val->LongLongInteger))
#endif

#ifndef GET_FP_VALUE
#define GET_FP_VALUE(Value)\
    ((Value)->Typ->Base == BaseType::TypeFloat ? Value->Val->Float : Value->Val->Double)
#endif

struct Table;
struct Picoc_Struct;
struct Value;

typedef struct Picoc_Struct Picoc;

/* lexical tokens */
#include "lextoken.hpp"

#include "Shadows.hpp"

/* hash table data structure */
struct TableEntry
{
    struct TableEntry *Next;        /* next item in this hash chain */
    const char *DeclFileName;       /* where the variable was declared */
    unsigned short DeclLine;
    unsigned short DeclColumn;

    union TableEntryPayload
    {
        struct ValueEntry
        {
            char *Key;              /* points to the shared string table */
            Value *Val;             /* the value we're storing */
            Shadows * ValShadows; /* shadowed values mapped by ScopeID */ // TODO the TableEntries are memset to 0, is that ok?
        } v;                        /* used for tables of values */

        char Key[1];                /* dummy size - used for the shared string table */

        struct BreakpointEntry      /* defines a breakpoint */
        {
            const char *FileName;
            unsigned short int Line;
            short int CharacterPos;
        } b;

    } p;
};

struct Table
{
    short Size;
    bool OnHeap;
    struct TableEntry **HashTable;
};

/* used in dynamic memory allocation */
struct AllocNode
{
    unsigned int Size;
    struct AllocNode *NextFree;
};

#include "RunMode.hpp"

/* how a condition was evaluated */
enum ConditionControl {
    ConditionTrue, ConditionFalse, ConditionUndefined
};

struct ValueList {
    struct ValueList * Next;
    const char * Identifier;

    ValueList() : Next(nullptr), Identifier(nullptr) {}
    ValueList(ValueList *next, const char *identifier) : Next(next), Identifier(identifier) {}
    virtual ~ValueList(){
        delete Next;
    }
};

/* parser state - has all this detail so we can parse nested files */
struct ParseState
{
    Picoc *pc;                  /* the picoc instance this parser is a part of */
    const unsigned char *Pos;   /* the character position in the source text */
    char *FileName;             /* what file we're executing (registered string) */
    size_t Line;             /* line number we're executing */
    short unsigned int CharacterPos;     /* character/column in the line we're executing */
    enum RunMode Mode;          /* whether to skip or run code */
    int SearchLabel;            /* what case label we're searching for */
    const char *SearchGotoLabel;/* what goto label we're searching for */
    const char *SourceText;     /* the entire source text */
    short int HashIfLevel;      /* how many "if"s we're nested down */
    short int HashIfEvaluateToLevel;    /* if we're not evaluating an if branch, what the last evaluated level was */
    enum ConditionControl LastConditionBranch;
    char DebugMode;             /* debugging mode */
    int ScopeID;                /* for keeping track of local variables (free them after they go out of scope) */
    // jsv:
    void (*DebuggerCallback)(ParseState*, bool, std::size_t const&); /* calls a callback when breakpoint reached */
    const char * EnterFunction;
    const char * CurrentFunction;
    const char * ReturnFromFunction;
    struct ValueList * ResolvedNonDetVars;
    char FreshGotoSearch;
    char SkipIntrinsic;
    Value *LastNonDetValue;
};


/* values */
#include "BaseType.hpp"

struct NonDetList {
    struct NonDetList * Next;
    bool IsNonDet;
};

/* data type */
struct ValueType
{
    enum BaseType Base;             /* what kind of type this is */
    int ArraySize;                  /* the size of an array type */
    int Sizeof;                     /* the storage required */
    int AlignBytes;                 /* the alignment boundary of this type */
    const char *Identifier;         /* the name of a struct or union */
    struct ValueType *FromType;     /* the type we're derived from (or nullptr) */
    struct ValueType *DerivedTypeList;  /* first in a list of types derived from this one */
    struct ValueType *Next;         /* next item in the derived type list */
    struct Table *Members;          /* members of a struct or union */
    ValueList *MemberOrder;         /* declaration order of members */
    int OnHeap;                     /* true if allocated on the heap */
    int StaticQualifier;            /* true if it's a static */
    // jsv
    bool IsNonDet;                  /* flag for when the variable is non-deterministic */
    struct NonDetList *NDList;     /* list of nd array element flags */
    int NDListSize;                 /* the number of elements allocated in the list*/
};

/* function definition */
struct FuncDef
{
    struct ValueType *ReturnType;   /* the return value type */
    int NumParams;                  /* the number of parameters */
    int VarArgs;                    /* has a variable number of arguments after the explicitly specified ones */
    struct ValueType **ParamType;   /* array of parameter types */
    char **ParamName;               /* array of parameter names */
    void (*Intrinsic)(struct ParseState *, Value *, Value **, int );            /* intrinsic call address or nullptr */
    struct ParseState Body;         /* lexical tokens of the function body if not intrinsic */
};

/* macro definition */
struct MacroDef
{
    int NumParams;                  /* the number of parameters */
    char **ParamName;               /* array of parameter names */
    struct ParseState Body;         /* lexical tokens of the function body if not intrinsic */
};

/* values */
union AnyValue
{
    char Character;
    short ShortInteger;
    int Integer;
    long LongInteger;
    long long LongLongInteger;
    unsigned short UnsignedShortInteger;
    unsigned int UnsignedInteger;
    unsigned long UnsignedLongInteger;
    unsigned long long UnsignedLongLongInteger;
    unsigned char UnsignedCharacter;
    double Double;
    float Float;
    void *Pointer;                  /* unsafe native pointers */
    char *Identifier;
    char ArrayMem[2];               /* placeholder for where the data starts, doesn't point to it */
    struct ValueType *Typ;
    struct FuncDef FuncDef;
    struct MacroDef MacroDef;
};

struct Value
{
    struct ValueType *Typ;          /* the type of this value */
    union AnyValue *Val;            /* pointer to the AnyValue which holds the actual content */
    Value *LValueFrom;       /* if an LValue, this is a Value our LValue is contained within (or nullptr) */
    int ScopeID;                    /* to know when it goes out of scope */
    char ValOnHeap;                 /* this Value is on the heap */
    char ValOnStack;                /* the AnyValue is on the stack along with this Value */
    char AnyValOnHeap;              /* the AnyValue is separately allocated from the Value on the heap */
    char IsLValue;                  /* is modifiable and is allocated somewhere we can usefully modify it */
    char OutOfScope;
    // jsv
    Value * ShadowedVal;             /* value that has been shadowed by it */
    char * VarIdentifier;           /* keeps track of the name of the variable this value belongs to */
    char ConstQualifier;            /* true if it's a const */
    int BitField;                  /* either 0 for nonbitfields or the length of the value */
    Value* ArrayRoot;               /* the array that this is a value of */
    int ArrayIndex;                 /* the index in the array that this represents */

//    int * dummy; // fixme : the MEM_ALIGN macro probably does not work, or I dont know... anyway - Values have to
                // fixme:   to be aligned so that the sizeof(Value) is divisible by ALIGN_TYPE macro
};


/* stack frame for function calls */
struct StackFrame
{
    struct ParseState ReturnParser;         /* how we got here */
    const char *FuncName;                   /* the name of the function we're in */
    Value *ReturnValue;              /* copy the return value here */
    Value **Parameter;               /* array of parameter values */
    int NumParams;                          /* the number of parameters */
    struct Table LocalTable;                /* the local variables and parameters */
    struct TableEntry *LocalHashTable[LOCAL_TABLE_SIZE];
    struct StackFrame *PreviousStackFrame;  /* the next lower stack frame */
};

/* lexer state */
enum LexMode
{
    LexModeNormal,
    LexModeHashInclude,
    LexModeHashDefine,
    LexModeHashDefineSpace,
    LexModeHashDefineSpaceIdent
};

struct LexState
{
    const char *Pos;
    const char *End;
    const char *FileName;
    int Line;
    int CharacterPos;
    const char *SourceText;
    enum LexMode Mode;
    int EmitExtraNewlines;
};

/* library function definition */
struct LibraryFunction
{
    void (*Func)(struct ParseState *Parser, Value *, Value **, int);
    const char *Prototype;
};

/* output stream-type specific state information */
union OutputStreamInfo
{
    struct StringOutputStream
    {
        struct ParseState *Parser;
        char *WritePos;
    } Str;
};

/* stream-specific method for writing characters to the console */
typedef void CharWriter(unsigned char, union OutputStreamInfo *);

/* used when writing output to a string - eg. sprintf() */
struct OutputStream
{
    CharWriter *Putch;
    union OutputStreamInfo i;
};

/* possible results of parsing a statement */
enum ParseResult { ParseResultEOF, ParseResultError, ParseResultOk };

/* a chunk of heap-allocated tokens we'll cleanup when we're done */
struct CleanupTokenNode
{
    void *Tokens;
    const char *SourceText;
    struct CleanupTokenNode *Next;
};

/* linked list of lexical tokens used in interactive mode */
struct TokenLine
{
    struct TokenLine *Next;
    unsigned char *Tokens;
    int NumBytes;
};


/* a list of libraries we can include */
struct IncludeLibrary
{
    char *IncludeName;
    void (*SetupFunction)(Picoc *pc);
    struct LibraryFunction *FuncList;
    const char *SetupCSource;
    struct IncludeLibrary *NextLib;
};

#define FREELIST_BUCKETS 8                          /* freelists for 4, 8, 12 ... 32 byte allocs */
#define SPLIT_MEM_THRESHOLD 16                      /* don't split memory which is close in size */
#define BREAKPOINT_TABLE_SIZE 21


/* the entire state of the picoc system */
struct Picoc_Struct
{
    /* parser global data */
    struct Table GlobalTable;
    struct CleanupTokenNode *CleanupTokenList;
    struct TableEntry *GlobalHashTable[GLOBAL_TABLE_SIZE];
    
    /* lexer global data */
    struct TokenLine *InteractiveHead;
    struct TokenLine *InteractiveTail;
    struct TokenLine *InteractiveCurrentLine;
    int LexUseStatementPrompt;
    union AnyValue LexAnyValue;
    Value LexValue;
    struct Table ReservedWordTable;
    struct TableEntry *ReservedWordHashTable[RESERVED_WORD_TABLE_SIZE];

    /* the table of string literal values */
    struct Table StringLiteralTable;
    struct TableEntry *StringLiteralHashTable[STRING_LITERAL_TABLE_SIZE];
    
    /* the stack */
    struct StackFrame *TopStackFrame;

    /* the value passed to exit() */
    int PicocExitValue;

    /* a list of libraries we can include */
    struct IncludeLibrary *IncludeLibList;

    /* heap memory */
#ifdef USE_MALLOC_STACK
    unsigned char *HeapMemory;          /* stack memory since our heap is malloc()ed */
    void *HeapBottom;                   /* the bottom of the (downward-growing) heap */
    void *StackFrame;                   /* the current stack frame */
    void *HeapStackTop;                 /* the top of the stack */
#else
    unsigned char HeapMemory[HEAP_SIZE];  /* all memory - stack and heap */
    void *HeapBottom;                   /* the bottom of the (downward-growing) heap */
    void *StackFrame;                   /* the current stack frame */
    void *HeapStackTop;                 /* the top of the stack */
#endif

    struct AllocNode *FreeListBucket[FREELIST_BUCKETS];      /* we keep a pool of freelist buckets to reduce fragmentation */
    struct AllocNode *FreeListBig;                           /* free memory which doesn't fit in a bucket */

    /* types */    
    struct ValueType UberType;
    struct ValueType IntType;
    struct ValueType ShortType;
    struct ValueType CharType;
    struct ValueType LongType;
    struct ValueType LongLongType;
    struct ValueType UnsignedIntType;
    struct ValueType UnsignedShortType;
    struct ValueType UnsignedLongType;
    struct ValueType UnsignedLongLongType;
    struct ValueType UnsignedCharType;
    struct ValueType DoubleType;
    struct ValueType FloatType;
    struct ValueType VoidType;
    struct ValueType TypeType;
    struct ValueType FunctionType;
    struct ValueType MacroType;
    struct ValueType EnumType;
    struct ValueType StructType;
    struct ValueType GotoLabelType;
    struct ValueType FunctionPtrType;
    struct ValueType *CharPtrType;
    struct ValueType *CharPtrPtrType;
    struct ValueType *CharArrayType;

    struct ValueType *StructPtrType;

    struct ValueType *VoidPtrType;

    /* ND types */
    struct ValueType IntNDType;
    struct ValueType ShortNDType;
    struct ValueType CharNDType;
    struct ValueType LongNDType;
    struct ValueType LongLongNDType;
    struct ValueType UnsignedIntNDType;
    struct ValueType UnsignedShortNDType;
    struct ValueType UnsignedCharNDType;
    struct ValueType UnsignedLongNDType;
    struct ValueType UnsignedLongLongNDType;
    struct ValueType FunctionPtrNDType;
    struct ValueType DoubleNDType;
    struct ValueType FloatNDType;
    /* debugger */
    struct Table BreakpointTable;
    struct TableEntry *BreakpointHashTable[BREAKPOINT_TABLE_SIZE];
    int BreakpointCount;
    int DebugManualBreak;
    
	/* Verifier Error Function Name, if any */
	const char *VerifierErrorFuncName;
    /* Whether the Error Function was called*/
    bool VerifierErrorFunctionWasCalled;
	
    /* C library */
    int BigEndian;
    int LittleEndian;

    IOFILE *CStdOut;
    IOFILE CStdOutBase;

    /* the picoc version string */
    const char *VersionString;
    
    /* exit longjump buffer */
#if defined(UNIX_HOST) || defined(WIN32)
    jmp_buf PicocExitBuf;
    jmp_buf AssumptionPicocExitBuf;
#endif
    int IsInAssumptionMode;
    
    /* string table */
    struct Table StringTable;
    struct TableEntry *StringHashTable[STRING_TABLE_SIZE];
    char *StrEmpty;
};

/* table.c */
namespace nitwit {
    namespace table {
        void TableInit(Picoc *pc);
        char* TableStrRegister(Picoc *pc, const char *Str, unsigned Len=0);
        void TableInitTable(Table *Tbl, TableEntry **HashTable, unsigned Size, bool OnHeap);
        int TableSet(Picoc *pc, Table *Tbl, char *Key, Value *Val, const char *DeclFileName, unsigned DeclLine, unsigned DeclColumn);
        int TableGet(Table *Tbl, const char *Key, Value **Val, const char **DeclFileName, unsigned *DeclLine, unsigned *DeclColumn);
        Value* TableDelete(Picoc *pc, Table *Tbl, const char *Key);
        char* TableSetIdentifier(Picoc *pc, Table *Tbl, const char *Ident, unsigned IdentLen);
        void TableStrFree(Picoc *pc);
        TableEntry *TableSearch(Table *Tbl, const char *Key, unsigned *AddAt);
    }
}

/* lex.c */
namespace nitwit {
    namespace lex {
        void LexInit(Picoc *pc);
        void LexCleanup(Picoc *pc);
        void *LexAnalyse(Picoc *pc, const char *FileName, const char *Source, int SourceLen, int *TokenLen);
        void LexInitParser(ParseState *Parser, Picoc *pc, const char *SourceText, void *TokenSource, char *FileName,
                           int RunIt, int EnableDebugger, void (*DebuggerCallback)(ParseState*, bool, std::size_t const&));
        LexToken LexGetToken(ParseState *Parser, Value **Value, bool IncPos);
        LexToken LexRawPeekToken(ParseState *Parser);
        void LexToEndOfLine(ParseState *Parser);
        void *LexCopyTokens(ParseState *StartParser, ParseState *EndParser);
        void LexInteractiveClear(Picoc *pc, ParseState *Parser);
        void LexInteractiveCompleted(Picoc *pc, ParseState *Parser);
        void LexInteractiveStatementPrompt(Picoc *pc);
    }
}

/* parse.c */
/* the following are defined in picoc.h:
 * void PicocParse(const char *FileName, const char *Source, int SourceLen, int RunIt, int CleanupNow, int CleanupSource);
 * void PicocParseInteractive(); */
namespace nitwit {
    namespace parse {
        void PicocParseInteractiveNoStartPrompt(Picoc *pc, int EnableDebugger);
        enum ParseResult ParseStatement(struct ParseState *Parser, int CheckTrailingSemicolon);
        Value *ParseFunctionDefinition(ParseState *Parser, ValueType *ReturnType, char *Identifier, bool IsPtrDecl);
        void ParseCleanup(Picoc *pc);
        void ParserCopyPos(struct ParseState *To, struct ParseState *From);
        void ParserCopy(struct ParseState *To, struct ParseState *From);
        void ConditionCallback(struct ParseState *Parser, bool Condition);
    }
}

/* expression.c */
namespace nitwit {
    namespace expressions {
        int ExpressionParse(struct ParseState* Parser, Value** Result);
        long long ExpressionParseLongLong(struct ParseState* Parser);
        void ExpressionAssign(struct ParseState* Parser, Value* DestValue, Value* SourceValue, int Force, const char* FuncName, int ParamNo, int AllowPointerCoercion);
    }
}

/* values.c */
void AdjustBitField(struct ParseState* Parser, struct Value *Val);

/* assumption_expr.c */
namespace nitwit {
    namespace assumptions {
        int ExpressionParse(struct ParseState* Parser, Value** Result);
        long long ExpressionParseLongLong(struct ParseState* Parser);
        void ExpressionAssign(struct ParseState* Parser, Value* DestValue, Value* SourceValue, int Force, const char* FuncName, int ParamNo, int AllowPointerCoercion);
    }
}

/* type.c */
void TypeInit(Picoc *pc);
void TypeCleanup(Picoc *pc);
int TypeSize(struct ValueType *Typ, int ArraySize, int Compact);
int TypeSizeValue(Value *Val, int Compact);
int TypeStackSizeValue(Value *Val);
int TypeLastAccessibleOffset(Picoc *pc, Value *Val);
int TypeParseFront(struct ParseState *Parser, struct ValueType **Typ, int *IsStatic, int *pInt);
void
TypeParseIdentPart(struct ParseState *Parser, struct ValueType *BasicTyp, struct ValueType **Typ, char **Identifier,
                   int *IsConst);
int TypeParseFunctionPointer(ParseState *Parser, ValueType *BasicType, ValueType **Type, char **Identifier, bool b);
ValueType *TypeParse(struct ParseState *Parser, struct ValueType **Typ, char **Identifier, int *IsStatic, int *IsConst,
                     bool IsArgument);
ValueType *TypeGetMatching(Picoc *pc, ParseState *Parser, ValueType *ParentType, BaseType Base, int ArraySize,
                           const char *Identifier, int AllowDuplicates, bool* IsNondet);
struct ValueType *TypeCreateOpaqueStruct(Picoc *pc, struct ParseState *Parser, const char *StructName, int Size);
int TypeIsForwardDeclared(struct ParseState *Parser, struct ValueType *Typ);
bool TypeIsNonDeterministic(struct ValueType *Typ);
/* array element non deterministic type functions */
void initNonDetList(struct ParseState * Parser, struct ValueType * Type, int ArraySize);
bool getNonDetListElement(NonDetList * List, int ArrayIndex);
void setNonDetListElement(NonDetList * List, int ArrayIndex, bool nonDet);
struct ValueType* TypeGetDeterministic(struct ParseState * Parser, struct ValueType * Typ);
struct ValueType* TypeGetNonDeterministic(struct ParseState * Parser, struct ValueType * Typ);
int TypeIsUnsigned(struct ValueType * Typ);

/* heap.c */
void HeapInit(Picoc *pc, int StackSize);
void HeapCleanup(Picoc *pc);
void *HeapAllocStack(Picoc *pc, int Size);
int HeapPopStack(Picoc *pc, void *Addr, int Size);
void HeapUnpopStack(Picoc *pc, int Size);
void HeapPushStackFrame(Picoc *pc);
int HeapPopStackFrame(Picoc *pc);
void *HeapAllocMem(Picoc *pc, int Size);
void HeapFreeMem(Picoc *pc, void *Mem);

/* variable.c */
void VariableInit(Picoc *pc);
void VariableCleanup(Picoc *pc);
void VariableFree(Picoc *pc, Value *Val);
void VariableTableCleanup(Picoc *pc, struct Table *HashTable);
void *VariableAlloc(Picoc *pc, struct ParseState *Parser, int Size, int OnHeap);
void VariableStackPop(struct ParseState *Parser, Value *Var);
Value *
VariableAllocValueAndData(Picoc *pc, struct ParseState *Parser, int DataSize, int IsLValue, Value *LValueFrom,
                          int OnHeap, char *VarIdentifier);
Value *VariableAllocValueAndCopy(Picoc *pc, struct ParseState *Parser, Value *FromValue, int OnHeap);
Value *VariableAllocValueFromType(Picoc *pc, struct ParseState *Parser, struct ValueType *Typ, int IsLValue, Value *LValueFrom, int OnHeap);
Value *
VariableAllocValueFromExistingData(struct ParseState *Parser, struct ValueType *Typ, union AnyValue *FromValue,
                                   int IsLValue, Value *LValueFrom, char *VarIdentifier);
Value *VariableAllocValueShared(struct ParseState *Parser, Value *FromValue);
Value *
VariableDefine(Picoc *pc, ParseState *Parser, char *Ident, Value *InitValue, ValueType *Typ, int MakeWritable, bool b);
Value *VariableDefineButIgnoreIdentical(struct ParseState *Parser, char *Ident, struct ValueType *Typ, int IsStatic, int *FirstVisit);
int VariableDefined(Picoc *pc, const char *Ident);
int VariableDefinedAndOutOfScope(Picoc *pc, const char *Ident);
void VariableRealloc(struct ParseState *Parser, Value *FromValue, int NewSize);
void VariableGet(Picoc *pc, struct ParseState *Parser, const char *Ident, Value **LVal);
Value* VariableDefinePlatformVar(Picoc *pc, struct ParseState *Parser, const char *Ident, struct ValueType *Typ, union AnyValue *FromValue, int IsWritable);
void VariableStackFrameAdd(struct ParseState *Parser, const char *FuncName, int NumParams);
void VariableStackFramePop(struct ParseState *Parser);
Value *VariableStringLiteralGet(Picoc *pc, char *Ident);
void VariableStringLiteralDefine(Picoc *pc, char *Ident, Value *Val);
void *VariableDereferencePointer(struct ParseState *Parser, Value *PointerValue, Value **DerefVal, int *DerefOffset, struct ValueType **DerefType, int *DerefIsLValue);
int VariableScopeBegin(struct ParseState * Parser, int* PrevScopeID);
void VariableScopeEnd(struct ParseState * Parser, int ScopeID, int PrevScopeID);
void ShadowTableCleanup(Picoc *pc, struct Table *HashTable);
/* clibrary.c */
void BasicIOInit(Picoc *pc);
void LibraryInit(Picoc *pc);
void LibraryAdd(Picoc *pc, struct Table *GlobalTable, const char *LibraryName, struct LibraryFunction *FuncList);
void CLibraryInit(Picoc *pc);
void PrintCh(char OutCh, IOFILE *Stream);
void PrintSimpleInt(long Num, IOFILE *Stream);
void PrintInt(long Num, int FieldWidth, int ZeroPad, int LeftJustify, IOFILE *Stream);
void PrintLongLong(long Num, int FieldWidth, int ZeroPad, int LeftJustify, IOFILE *Stream);
void PrintStr(const char *Str, IOFILE *Stream);
void PrintFP(double Num, IOFILE *Stream);
void PrintType(struct ValueType *Typ, IOFILE *Stream);
void LibPrintf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs);

/* platform.c */
/* the following are defined in picoc.h:
 * void PicocCallMain(int argc, char **argv);
 * int PicocPlatformSetExitPoint();
 * void PicocInitialise(int StackSize);
 * void PicocCleanup();
 * void PicocPlatformScanFile(const char *FileName);
 * extern int PicocExitValue; */
void PrintSourceTextErrorLine(IOFILE *Stream, const char *FileName, const char *SourceText, int Line, int CharacterPos);
void ProgramFail(struct ParseState *Parser, const char *Message, ...);
void ProgramFailWithExitCode(struct ParseState *Parser, int exitCode, const char *Message, ...);
void ProgramFailNoParser(Picoc *pc, const char *Message, ...);
void ProgramFailNoParserWithExitCode(Picoc* pc, int exitCode, const char* Message, ...);
void AssignFail(struct ParseState *Parser, const char *Format, struct ValueType *Type1, struct ValueType *Type2, int Num1, int Num2, const char *FuncName, int ParamNo);
void LexFail(Picoc *pc, struct LexState *Lexer, const char *Message, ...);
void PlatformInit(Picoc *pc);
void PlatformCleanup(Picoc *pc);
char *PlatformGetLine(char *Buf, int MaxLen, const char *Prompt);
int PlatformGetCharacter();
void PlatformPutc(unsigned char OutCh, union OutputStreamInfo *);
void PlatformPrintf(IOFILE *Stream, const char *Format, ...);
void PlatformVPrintf(IOFILE *Stream, const char *Format, va_list Args);
void PlatformExit(Picoc *pc, int ExitVal);
char *PlatformMakeTempName(Picoc *pc, char *TempNameBuffer);
void PlatformLibraryInit(Picoc *pc);

/* include.c */
void IncludeInit(Picoc *pc);
void IncludeCleanup(Picoc *pc);
void IncludeRegister(Picoc *pc, const char *IncludeName, void (*SetupFunction)(Picoc *pc), struct LibraryFunction *FuncList, const char *SetupCSource);
void IncludeFile(Picoc *pc, char *Filename);
/* the following is defined in picoc.h:
 * void PicocIncludeAllSystemHeaders(); */
 
/* debug.c */
void DebugInit(Picoc *pc);
void DebugCleanup(Picoc *pc);
void DebugCheckStatement(ParseState* Parser, bool wasMultiLineDeclaration, std::size_t const& lastLine);
void DebugSetBreakpoint(ParseState* Parser);


char *GetGotoIdentifier(const char *function_id, const char *goto_id);

/* stdio.c */
extern const char StdioDefs[];
extern struct LibraryFunction StdioFunctions[];
void StdioSetupFunc(Picoc *pc);

/* math.c */
extern struct LibraryFunction MathFunctions[];
void MathSetupFunc(Picoc *pc);

/* string.c */
extern struct LibraryFunction StringFunctions[];
void StringSetupFunc(Picoc *pc);

/* stdlib.c */
extern struct LibraryFunction StdlibFunctions[];
void StdlibSetupFunc(Picoc *pc);

/* time.c */
extern const char StdTimeDefs[];
extern struct LibraryFunction StdTimeFunctions[];
void StdTimeSetupFunc(Picoc *pc);

/* errno.c */
void StdErrnoSetupFunc(Picoc *pc);

/* ctype.c */
extern struct LibraryFunction StdCTypeFunctions[];

/* stdbool.c */
extern const char StdboolDefs[];
void StdboolSetupFunc(Picoc *pc);

/* unistd.c */
extern const char UnistdDefs[];
extern struct LibraryFunction UnistdFunctions[];
void UnistdSetupFunc(Picoc *pc);

// verif.c
extern const char VerifDefs[];
extern struct LibraryFunction VerifFunctions[];
void VerifSetupFunc(Picoc *pc);

// assert.c
extern struct LibraryFunction AssertFunctions[];

#include "verbose.hpp"

#endif /* INTERPRETER_H */
