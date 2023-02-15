/* picoc lexer - converts source text into a tokenised form */

#include "interpreter.hpp"

namespace nitwit {
    namespace lex {

#ifdef NO_CTYPE
#define isalpha(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define isdigit(c) ((c) >= '0' && (c) <= '9')
#define isalnum(c) (isalpha(c) || isdigit(c))
#define isspace(c) ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
#endif
#define isCidstart(c) (isalpha(c) || (c)=='_' || (c)=='#')
#define isCident(c) (isalnum(c) || (c)=='_' || (c) == '$') // allow dollar sign in identifier

#define IS_HEX_ALPHA_DIGIT(c) (((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#define IS_BASE_DIGIT(c,b) (((c) >= '0' && (c) < '0' + (((b)<10)?(b):10)) || (((b) > 10) ? IS_HEX_ALPHA_DIGIT(c) : FALSE))
#define GET_BASE_DIGIT(c) (((c) <= '9') ? ((c) - '0') : (((c) <= 'F') ? ((c) - 'A' + 10) : ((c) - 'a' + 10)))

#define NEXTIS(c,x,y) { if (NextChar == (c)) { LEXER_INC(Lexer); GotToken = (x); } else GotToken = (y); }
#define NEXTIS3(c,x,d,y,z) { if (NextChar == (c)) { LEXER_INC(Lexer); GotToken = (x); } else NEXTIS(d,y,z) }
#define NEXTIS4(c,x,d,y,e,z,a) { if (NextChar == (c)) { LEXER_INC(Lexer); GotToken = (x); } else NEXTIS3(d,y,e,z,a) }
#define NEXTIS3PLUS(c,x,d,y,e,z,a) { if (NextChar == (c)) { LEXER_INC(Lexer); GotToken = (x); } else if (NextChar == (d)) { if (Lexer->Pos[1] == (e)) { LEXER_INCN(Lexer, 2); GotToken = (z); } else { LEXER_INC(Lexer); GotToken = (y); } } else GotToken = (a); }
#define NEXTISEXACTLY3(c,d,y,z) { if (NextChar == (c) && Lexer->Pos[1] == (d)) { LEXER_INCN(Lexer, 2); GotToken = (y); } else GotToken = (z); }

#define LEXER_INC(l) ( (l)->Pos++, (l)->CharacterPos++ )
#define LEXER_INCN(l, n) ( (l)->Pos+=(n), (l)->CharacterPos+=(n) )
#define TOKEN_DATA_OFFSET 2

#define MAX_CHAR_VALUE 255      /* maximum value which can be represented by a "char" data type */

LexToken LexScanGetToken(Picoc *pc, LexState *Lexer, Value **Value);
struct ReservedWord
{
    const char *Word;
    LexToken Token;
};

static struct ReservedWord ReservedWords[] =
{
    { "#define", TokenHashDefine },
    { "#else", TokenHashElse },
    { "#endif", TokenHashEndif },
    { "#if", TokenHashIf },
    { "#ifdef", TokenHashIfdef },
    { "#ifndef", TokenHashIfndef },
    { "#include", TokenHashInclude },
    { "auto", TokenAutoType },
    { "break", TokenBreak },
    { "case", TokenCase },
    { "char", TokenCharType },
    { "continue", TokenContinue },
    { "default", TokenDefault },
    { "delete", TokenDelete },
    { "do", TokenDo },
    { "double", TokenDoubleType },
    { "else", TokenElse },
    { "enum", TokenEnumType },
    { "extern", TokenExternType },
    { "float", TokenFloatType },
    { "for", TokenFor },
    { "goto", TokenGoto },
    { "if", TokenIf },
    { "int", TokenIntType },
    { "long", TokenLongType },
//    { "new", TokenNew }, ignore
    { "register", TokenRegisterType },
    { "return", TokenReturn },
    { "short", TokenShortType },
    { "signed", TokenSignedType },
    { "__signed__", TokenSignedType },
    { "sizeof", TokenSizeof },
    { "static", TokenStaticType },
    { "struct", TokenStructType },
    { "switch", TokenSwitch },
    { "typedef", TokenTypedef },
    { "union", TokenUnionType },
    { "unsigned", TokenUnsignedType },
    { "void", TokenVoidType },
    { "while", TokenWhile },
    // jsv
    { "__attribute__", TokenAttribute },
    { "__noreturn__", TokenNoReturn },
#ifdef USE_BASIC_CONST
    { "const", TokenConst },
    { "__const", TokenConst },
#else
    { "const", TokenIgnore },
    { "__const", TokenIgnore },
#endif
    { "restrict", TokenIgnore },
    { "__restrict", TokenIgnore },
    { "__extension__", TokenIgnore },
    { "inline", TokenIgnore },
    { "__inline", TokenIgnore },
    { "__inline__", TokenIgnore },
    { "__builtin_va_list", TokenIgnore },
    { "volatile", TokenIgnore },
    { "#pragma", TokenPragma },
    { "\\result", TokenWitnessResult },
};


/* initialise the lexer */
void LexInit(Picoc *pc)
{
    nitwit::table::TableInitTable(&pc->ReservedWordTable, &pc->ReservedWordHashTable[0], RESERVED_WORD_TABLE_SIZE, true);

    for (unsigned Count = 0; Count < sizeof(ReservedWords) / sizeof(ReservedWord); Count++)
    {
        nitwit::table::TableSet(pc, &pc->ReservedWordTable, nitwit::table::TableStrRegister(pc, ReservedWords[Count].Word), (Value *)&ReservedWords[Count], nullptr, 0, 0);
    }

    pc->LexValue.Typ = nullptr;
    pc->LexValue.Val = &pc->LexAnyValue;
    pc->LexValue.LValueFrom = FALSE;
    pc->LexValue.ValOnHeap = FALSE;
    pc->LexValue.ValOnStack = FALSE;
    pc->LexValue.AnyValOnHeap = FALSE;
    pc->LexValue.IsLValue = FALSE;
}

/* deallocate */
void LexCleanup(Picoc *pc)
{
    LexInteractiveClear(pc, nullptr);

    for (unsigned Count = 0; Count < sizeof(ReservedWords) / sizeof(ReservedWord); Count++)
        nitwit::table::TableDelete(pc, &pc->ReservedWordTable, nitwit::table::TableStrRegister(pc, ReservedWords[Count].Word));
}

/* check if a word is a reserved word - used while scanning */
LexToken LexCheckReservedWord(Picoc *pc, const char *Word)
{
    Value *val;

    if (nitwit::table::TableGet(&pc->ReservedWordTable, Word, &val, nullptr, nullptr, nullptr))
        return ((ReservedWord *)val)->Token;
    else
        return TokenNone;
}

/* get a numeric literal - used while scanning */
LexToken LexGetNumber(Picoc *pc, LexState *Lexer, Value *Value)
{
    long long Result = 0;
    long Base = 10;
    LexToken ResultToken;
    char Unsigned = FALSE, LongLong = FALSE;
    float FResult;
    float FDiv;
    double DResult;
    double DDiv;
    /* long/unsigned flags */
#if 0 /* unused for now */
    char IsLong = 0;
    char IsUnsigned = 0;
#endif

    if (*Lexer->Pos == '0') {
        /* a binary, octal or hex literal */
        LEXER_INC(Lexer);
        if (Lexer->Pos != Lexer->End)
        {
            if (*Lexer->Pos == 'x' || *Lexer->Pos == 'X')
                { Base = 16; LEXER_INC(Lexer); }
            else if (*Lexer->Pos == 'b' || *Lexer->Pos == 'B')
                { Base = 2; LEXER_INC(Lexer); }
            else if (*Lexer->Pos != '.')
                Base = 8;
        }
    }

    /* get the value */
    for (; Lexer->Pos != Lexer->End && IS_BASE_DIGIT(*Lexer->Pos, Base); LEXER_INC(Lexer))
        Result = Result * Base + GET_BASE_DIGIT(*Lexer->Pos);

    Value->Typ = &pc->LongType;
    if (*Lexer->Pos == 'u' || *Lexer->Pos == 'U')
    {
        LEXER_INC(Lexer);
        Unsigned = TRUE;
        Value->Typ = &pc->UnsignedLongType;
    }
    if (*Lexer->Pos == 'l' || *Lexer->Pos == 'L')
    {
        LEXER_INC(Lexer);
    }
    if (*Lexer->Pos == 'l' || *Lexer->Pos == 'L')
    {
        LEXER_INC(Lexer);
        LongLong = TRUE;
    }

    if (Unsigned){
        if (LongLong){
            Value->Val->UnsignedLongLongInteger = Result;
            ResultToken = TokenUnsignedLLConstanst;
        } else {
            Value->Val->UnsignedLongInteger = (unsigned long) Result;
            ResultToken = TokenUnsignedIntConstanst;
        }
    } else {
        if (LongLong){
            Value->Val->LongLongInteger = Result;
            ResultToken = TokenLLConstanst;
        } else {
            Value->Val->LongInteger = (long) Result;
            ResultToken = TokenIntegerConstant;
        }
    }


    if (Lexer->Pos == Lexer->End)
        return ResultToken;

    if (Lexer->Pos == Lexer->End)
    {
        return ResultToken;
    }

    if (*Lexer->Pos != '.' && *Lexer->Pos != 'e' && *Lexer->Pos != 'E')
    {
        return ResultToken;
    }

    DResult = (double)Result;
    FResult = (float)Result;

    if (*Lexer->Pos == '.')
    {
        LEXER_INC(Lexer);
        for (DDiv = 1.0/Base, FDiv = 1.0 / Base; Lexer->Pos != Lexer->End && IS_BASE_DIGIT(*Lexer->Pos, Base); LEXER_INC(Lexer), DDiv /= (double)Base, FDiv /= (float)Base)
        {
            DResult += GET_BASE_DIGIT(*Lexer->Pos) * DDiv;
            FResult += GET_BASE_DIGIT(*Lexer->Pos) * FDiv;
        }
    }

    if (Lexer->Pos != Lexer->End && (*Lexer->Pos == 'e' || *Lexer->Pos == 'E'))
    {
        int ExponentSign = 1;

        LEXER_INC(Lexer);
        if (Lexer->Pos != Lexer->End && *Lexer->Pos == '-')
        {
            ExponentSign = -1;
            LEXER_INC(Lexer);
        } else if (Lexer->Pos != Lexer->End && *Lexer->Pos == '+')
        {
            ExponentSign = 1;
            LEXER_INC(Lexer);
        }

        Result = 0;
        while (Lexer->Pos != Lexer->End && IS_BASE_DIGIT(*Lexer->Pos, Base))
        {
            Result = Result * Base + GET_BASE_DIGIT(*Lexer->Pos);
            LEXER_INC(Lexer);
        }

#ifdef DEBUG_EXPRESSIONS
        printf("LEX - Using Base %.17g and exp %.17g, prior result is %.17g\n", (double)Base, (double)Result* ExponentSign, DResult);
#endif
        while (Result >= 10) {
            Result -= 10;
            DResult *= pow((double)Base, (double)10.0 * ExponentSign);
            FResult *= pow((float)Base, (float)10.0f * ExponentSign);
        }
        if (Result > 0) {
            DResult *= pow((double)Base, (double)Result * ExponentSign);
            FResult *= pow((float)Base, (float)Result * ExponentSign);
        }
    }

    bool const isFp = (*Lexer->Pos == 'f' || *Lexer->Pos == 'F');
    if (isFp) {
#ifdef DEBUG_EXPRESSIONS
        printf("LEX - Found float constant %.17g (double: %.17g)\n", FResult, DResult);
#endif
        Value->Typ = &pc->FloatType;
        Value->Val->Float = FResult;
    }
    else {
#ifdef DEBUG_EXPRESSIONS
        printf("LEX - Found double constant %.17g\n", DResult);
#endif
        Value->Typ = &pc->DoubleType;
        Value->Val->Double = DResult;
    }

    if (*Lexer->Pos == 'f' || *Lexer->Pos == 'F') {
        LEXER_INC(Lexer);
    } else if (*Lexer->Pos == 'l' || *Lexer->Pos == 'L') {
        LEXER_INC(Lexer);
    }

    return isFp ? TokenFloatConstant : TokenDoubleConstant;
}

/* get a reserved word or identifier - used while scanning */
LexToken LexGetWord(Picoc *pc, LexState *Lexer, Value *Value)
{
    const char *StartPos = Lexer->Pos;
    LexToken Token;

    do {
        LEXER_INC(Lexer);
    } while (Lexer->Pos != Lexer->End && isCident((int)*Lexer->Pos));

    Value->Typ = nullptr;
    Value->Val->Identifier = nitwit::table::TableStrRegister(pc, StartPos, Lexer->Pos - StartPos);

    Token = LexCheckReservedWord(pc, Value->Val->Identifier);
    switch (Token)
    {
        case TokenHashInclude: Lexer->Mode = LexModeHashInclude; break;
        case TokenHashDefine: Lexer->Mode = LexModeHashDefine; break;
        case TokenIgnore: return LexScanGetToken(pc, Lexer, &Value);
        case TokenAttribute:
            // just ignore the __attributes__
            for (; Lexer->Pos != Lexer->End && *Lexer->Pos != ';'
                    ; LEXER_INC(Lexer)) {
                if (*Lexer->Pos == '\n')
                    ++Lexer->EmitExtraNewlines;
            }
            return TokenSemicolon;
        case TokenPragma:
            // just ignore the pragmas
            for (; Lexer->Pos != Lexer->End && *Lexer->Pos != '\n'
                    ; LEXER_INC(Lexer)) {}
            return TokenSemicolon;
        default: break;
    }

    if (Token != TokenNone)
        return Token;

    if (Lexer->Mode == LexModeHashDefineSpace)
        Lexer->Mode = LexModeHashDefineSpaceIdent;

    return TokenIdentifier;
}

/* unescape a character from an octal character constant */
unsigned char LexUnEscapeCharacterConstant(const char **From, const char *End, unsigned char FirstChar, int Base)
{
    unsigned char Total = GET_BASE_DIGIT(FirstChar);
    int CCount;
    for (CCount = 0; IS_BASE_DIGIT(**From, Base) && CCount < 2; CCount++, (*From)++)
        Total = Total * Base + GET_BASE_DIGIT(**From);

    return Total;
}

/* unescape a character from a string or character constant */
unsigned char LexUnEscapeCharacter(const char **From, const char *End)
{
    unsigned char ThisChar;

    while (*From != End && **From == '\\' &&
            &(*From)[1] != End && (*From)[1] == '\n' )
        (*From) += 2;       /* skip escaped end of lines with LF line termination */

    while (*From != End && **From == '\\' &&
            &(*From)[1] != End && &(*From)[2] != End && (*From)[1] == '\r' && (*From)[2] == '\n')
        (*From) += 3;       /* skip escaped end of lines with CR/LF line termination */

    if (*From == End)
        return '\\';

    if (**From == '\\') {
        /* it's escaped */
        (*From)++;
        if (*From == End)
            return '\\';

        ThisChar = *(*From)++;
        switch (ThisChar)
        {
            case '\\':
                return '\\';
            case '\'': return '\'';
            case '"':  return '"';
            case 'a':  return '\a';
            case 'b':  return '\b';
            case 'f':  return '\f';
            case 'n':  return '\n';
            case 'r':  return '\r';
            case 't':  return '\t';
            case 'v':  return '\v';
            case '0': case '1': case '2': case '3': return LexUnEscapeCharacterConstant(From, End, ThisChar, 8);
            case 'x': return LexUnEscapeCharacterConstant(From, End, '0', 16);
            default:   return ThisChar;
        }
    }
    else
        return *(*From)++;
}

/* get a string constant - used while scanning */
LexToken LexGetStringConstant(Picoc *pc, LexState *Lexer, Value *Value, char EndChar)
{
    bool Escape = false;
    const char *StartPos = Lexer->Pos;
    const char *EndPos;
    char *EscBuf;
    char *EscBufPos;
    char *RegString;
    struct Value *ArrayValue;

    while (Lexer->Pos != Lexer->End && (*Lexer->Pos != EndChar || Escape)) {
        /* find the end */
        if (Escape)
        {
            if (*Lexer->Pos == '\r' && Lexer->Pos+1 != Lexer->End)
                Lexer->Pos++;

            if (*Lexer->Pos == '\n' && Lexer->Pos+1 != Lexer->End)
            {
                Lexer->Line++;
                Lexer->Pos++;
                Lexer->CharacterPos = 0;
                Lexer->EmitExtraNewlines++;
            }

            Escape = false;
        }
        else if (*Lexer->Pos == '\\') {
            Escape = true;
        }

        LEXER_INC(Lexer);
    }
    EndPos = Lexer->Pos;

    EscBuf = static_cast<char *>(HeapAllocStack(pc, EndPos - StartPos));
    if (EscBuf == nullptr)
        LexFail(pc, Lexer, "out of memory");

    for (EscBufPos = EscBuf, Lexer->Pos = StartPos; Lexer->Pos != EndPos;)
        *EscBufPos++ = LexUnEscapeCharacter(&Lexer->Pos, EndPos);

    /* try to find an existing copy of this string literal */
    RegString = nitwit::table::TableStrRegister(pc, EscBuf, EscBufPos - EscBuf);
    HeapPopStack(pc, EscBuf, EndPos - StartPos);
    ArrayValue = VariableStringLiteralGet(pc, RegString);
    if (ArrayValue == nullptr)
    {
        /* create and store this string literal */
        ArrayValue = VariableAllocValueAndData(pc, nullptr, 0, FALSE, nullptr, TRUE, nullptr);
        ArrayValue->Typ = pc->CharArrayType;
        ArrayValue->Val = (union AnyValue *)RegString;
        VariableStringLiteralDefine(pc, RegString, ArrayValue);
    }

    /* create the the pointer for this char* */
    Value->Typ = pc->CharPtrType;
    Value->Val->Pointer = RegString;
    if (*Lexer->Pos == EndChar)
        LEXER_INC(Lexer);

    return TokenStringConstant;
}

/* get a character constant - used while scanning */
LexToken LexGetCharacterConstant(Picoc *pc, LexState *Lexer, Value *Value)
{
    Value->Typ = &pc->CharType;
    Value->Val->Character = LexUnEscapeCharacter(&Lexer->Pos, Lexer->End);
    if (Lexer->Pos != Lexer->End && *Lexer->Pos != '\'')
        LexFail(pc, Lexer, "expected \"'\"");

    LEXER_INC(Lexer);
    return TokenCharacterConstant;
}

/* skip a comment - used while scanning */
void LexSkipComment(LexState *Lexer, char NextChar, LexToken *ReturnToken)
{
    if (NextChar == '*') {
        /* conventional C comment */
        while (Lexer->Pos != Lexer->End && (*(Lexer->Pos-1) != '*' || *Lexer->Pos != '/'))
        {
            if (*Lexer->Pos == '\n')
                Lexer->EmitExtraNewlines++;

            LEXER_INC(Lexer);
        }

        if (Lexer->Pos != Lexer->End)
            LEXER_INC(Lexer);

        Lexer->Mode = LexModeNormal;
    }
    else {
        /* C++ style comment */
        // Or also a preprocess comment '# '
        while (Lexer->Pos != Lexer->End && *Lexer->Pos != '\n')
            LEXER_INC(Lexer);
    }
}

/* get a single token from the source - used while scanning */
LexToken LexScanGetToken(Picoc *pc, LexState *Lexer, Value **Value)
{
    char ThisChar;
    char NextChar;
    LexToken GotToken = TokenNone;

    /* handle cases line multi-line comments or string constants which mess up the line count */
    if (Lexer->EmitExtraNewlines > 0)
    {
        Lexer->EmitExtraNewlines--;
        return TokenEndOfLine;
    }

    /* scan for a token */
    do
    {
        *Value = &pc->LexValue;
        while (Lexer->Pos != Lexer->End && isspace((int)*Lexer->Pos))
        {
            if (*Lexer->Pos == '\n')
            {
                Lexer->Line++;
                Lexer->Pos++;
                Lexer->Mode = LexModeNormal;
                Lexer->CharacterPos = 0;
                return TokenEndOfLine;
            }
            else if (Lexer->Mode == LexModeHashDefine || Lexer->Mode == LexModeHashDefineSpace)
                Lexer->Mode = LexModeHashDefineSpace;

            else if (Lexer->Mode == LexModeHashDefineSpaceIdent)
                Lexer->Mode = LexModeNormal;

            LEXER_INC(Lexer);
        }

        if (Lexer->Pos == Lexer->End || *Lexer->Pos == '\0')
            return TokenEOF;


        ThisChar = *Lexer->Pos;
        NextChar = (Lexer->Pos + 1 != Lexer->End) ? *(Lexer->Pos + 1) : 0;

        // Handle Compiler comments from preprocessing: '# NUMBER'
        if ((ThisChar == '#') && (NextChar == ' ')) {
            while (Lexer->Pos != Lexer->End && *Lexer->Pos != '\n')
                LEXER_INC(Lexer);
            return LexScanGetToken(pc, Lexer, Value);
        }

        if (isCidstart((int)ThisChar) || ThisChar == '\\')
            return LexGetWord(pc, Lexer, *Value);

        if (isdigit((int)ThisChar))
            return LexGetNumber(pc, Lexer, *Value);

        
        LEXER_INC(Lexer);
        switch (ThisChar)
        {
            case '"': GotToken = LexGetStringConstant(pc, Lexer, *Value, '"'); break;
            case '\'': GotToken = LexGetCharacterConstant(pc, Lexer, *Value); break;
            case '(': if (Lexer->Mode == LexModeHashDefineSpaceIdent) GotToken = TokenOpenMacroBracket; else GotToken = TokenOpenBracket; Lexer->Mode = LexModeNormal; break;
            case ')': GotToken = TokenCloseBracket; break;
            case '=': NEXTIS('=', TokenEqual, TokenAssign); if (GotToken == TokenAssign && pc->IsInAssumptionMode) GotToken = TokenEqual; break;
            case '+': NEXTIS3('=', TokenAddAssign, '+', TokenIncrement, TokenPlus); break;
            case '-': NEXTIS4('=', TokenSubtractAssign, '>', TokenArrow, '-', TokenDecrement, TokenMinus); break;
            case '*': NEXTIS('=', TokenMultiplyAssign, TokenAsterisk); break;
            case '/': if (NextChar == '/' || NextChar == '*') { LEXER_INC(Lexer); LexSkipComment(Lexer, NextChar, &GotToken); } else NEXTIS('=', TokenDivideAssign, TokenSlash); break;
            case '%': NEXTIS('=', TokenModulusAssign, TokenModulus); break;
            case '<':
                if (Lexer->Mode == LexModeHashInclude) GotToken = LexGetStringConstant(pc, Lexer, *Value, '>');
                else {NEXTIS3PLUS('=', TokenLessEqual, '<', TokenShiftLeft, '=', TokenShiftLeftAssign, TokenLessThan); }
                break;
            case '>': NEXTIS3PLUS('=', TokenGreaterEqual, '>', TokenShiftRight, '=', TokenShiftRightAssign, TokenGreaterThan); break;
            case ';': GotToken = TokenSemicolon; break;
            case '&': NEXTIS3('=', TokenArithmeticAndAssign, '&', TokenLogicalAnd, TokenAmpersand); break;
            case '|': NEXTIS3('=', TokenArithmeticOrAssign, '|', TokenLogicalOr, TokenArithmeticOr); break;
            case '{': GotToken = TokenLeftBrace; break;
            case '}': GotToken = TokenRightBrace; break;
            case '[': GotToken = TokenLeftSquareBracket; break;
            case ']': GotToken = TokenRightSquareBracket; break;
            case '!': NEXTIS('=', TokenNotEqual, TokenUnaryNot); break;
            case '^': NEXTIS('=', TokenArithmeticExorAssign, TokenArithmeticExor); break;
            case '~': GotToken = TokenUnaryExor; break;
            case ',': GotToken = TokenComma; break;
            case '.': NEXTISEXACTLY3('.', '.', TokenEllipsis, TokenDot); break;
            case '?': GotToken = TokenQuestionMark; break;
            case ':': GotToken = TokenColon; break;
            default:  LexFail(pc, Lexer, "illegal character '%c'", ThisChar); break;
        }
    } while (GotToken == TokenNone);

    return GotToken;
}

/* what size value goes with each token */
int LexTokenSize(LexToken const& Token)
{
    switch (Token)
    {
        case TokenIdentifier: case TokenStringConstant: return sizeof(char *);
        case TokenIntegerConstant: return sizeof(long);
        case TokenUnsignedIntConstanst: return sizeof(unsigned long);
        case TokenLLConstanst: return sizeof(long long);
        case TokenUnsignedLLConstanst: return sizeof(unsigned long long);
        case TokenCharacterConstant: return sizeof(unsigned char);
        case TokenFloatConstant: return sizeof(float);
        case TokenDoubleConstant: return sizeof(double);
        default: return 0;
    }
}

/* produce tokens from the lexer and return a heap buffer with the result - used for scanning */
void *LexTokenise(Picoc *pc, LexState *Lexer, int *TokenLen)
{
    LexToken Token;
    void *HeapMem;
    Value *GotValue;
    int MemUsed = 0;
    int ValueSize;
    int ReserveSpace = (Lexer->End - Lexer->Pos) * 4 + 16;
    void *TokenSpace = HeapAllocStack(pc, ReserveSpace);
    char *TokenPos = (char *)TokenSpace;
    int LastCharacterPos = 0;

    if (TokenSpace == nullptr)
        LexFail(pc, Lexer, "out of memory");

    do {
        /* store the token at the end of the stack area */
        Token = LexScanGetToken(pc, Lexer, &GotValue);

#ifdef DEBUG_LEXER
        printf("Token: %02x\n", Token);
#endif
        *(unsigned char *)TokenPos = Token;
        TokenPos++;
        MemUsed++;

        *(unsigned char *)TokenPos = (unsigned char)LastCharacterPos;
        TokenPos++;
        MemUsed++;

        ValueSize = LexTokenSize(Token);
        if (ValueSize > 0) {
            /* store a value as well */
            memcpy((void *)TokenPos, (void *)GotValue->Val, ValueSize);
            TokenPos += ValueSize;
            MemUsed += ValueSize;
        }

        LastCharacterPos = Lexer->CharacterPos;

    } while (Token != TokenEOF);

    HeapMem = HeapAllocMem(pc, MemUsed);
    if (HeapMem == nullptr)
        LexFail(pc, Lexer, "out of memory");

    assert(ReserveSpace >= MemUsed);
    memcpy(HeapMem, TokenSpace, MemUsed);
    HeapPopStack(pc, TokenSpace, ReserveSpace);
#ifdef DEBUG_LEXER
    {
        int Count;
        printf("Tokens: ");
        for (Count = 0; Count < MemUsed; Count++)
            printf("%02x ", *((unsigned char *)HeapMem+Count));
        printf("\n");
    }
#endif
    if (TokenLen)
        *TokenLen = MemUsed;

    return HeapMem;
}

/* lexically analyse some source text */
void *LexAnalyse(Picoc *pc, const char *FileName, const char *Source, int SourceLen, int *TokenLen)
{
    LexState Lexer;

    Lexer.Pos = Source;
    Lexer.End = Source + SourceLen;
    Lexer.Line = 1;
    Lexer.FileName = FileName;
    Lexer.Mode = LexModeNormal;
    Lexer.EmitExtraNewlines = 0;
    Lexer.CharacterPos = 1;
    Lexer.SourceText = Source;

    return LexTokenise(pc, &Lexer, TokenLen);
}

/* prepare to parse a pre-tokenised buffer */
void LexInitParser(ParseState *Parser, Picoc *pc, const char *SourceText, void *TokenSource, char *FileName,
                   int RunIt, int EnableDebugger, void (*DebuggerCallback)(ParseState*, bool, std::size_t const&))
{
    Parser->pc = pc;
    Parser->Pos = static_cast<const unsigned char *>(TokenSource);
    Parser->Line = 1;
    Parser->FileName = FileName;
    Parser->Mode = RunIt ? RunMode::RunModeRun : RunMode::RunModeSkip;
    Parser->SearchLabel = 0;
    Parser->HashIfLevel = 0;
    Parser->HashIfEvaluateToLevel = 0;
    Parser->LastConditionBranch = ConditionUndefined;
    Parser->CharacterPos = 0;
    Parser->SourceText = SourceText;
    Parser->DebugMode = EnableDebugger;
    // jsv
    Parser->ScopeID = 0;
    Parser->DebuggerCallback = DebuggerCallback;
    Parser->EnterFunction = nullptr;
    Parser->ReturnFromFunction = nullptr;
    Parser->CurrentFunction = nullptr;
    Parser->ResolvedNonDetVars = nullptr;
    Parser->FreshGotoSearch = FALSE;
    Parser->SkipIntrinsic = FALSE;
    Parser->LastNonDetValue = nullptr;
}

/* get the next token, without pre-processing */
LexToken LexGetRawToken(ParseState *Parser, Value **Value, bool IncPos)
{
    LexToken Token = TokenNone;
    int ValueSize;
    char *Prompt = nullptr;
    Picoc *pc = Parser->pc;

    do {
        /* get the next token */
        if (Parser->Pos == nullptr && pc->InteractiveHead != nullptr)
            Parser->Pos = pc->InteractiveHead->Tokens;

        if (Parser->FileName != pc->StrEmpty || pc->InteractiveHead != nullptr) {
            /* skip leading newlines */
            while ((Token = (LexToken)*(unsigned char *)Parser->Pos) == TokenEndOfLine)
            {
                Parser->Line++;
                Parser->Pos += TOKEN_DATA_OFFSET;
            }
        }

        if (Parser->FileName == pc->StrEmpty && (pc->InteractiveHead == nullptr || Token == TokenEOF)) {
            /* we're at the end of an interactive input token list */
            char LineBuffer[LINEBUFFER_MAX];
            void *LineTokens;
            int LineBytes;
            TokenLine *LineNode;

            if (pc->InteractiveHead == nullptr || (unsigned char *)Parser->Pos == &pc->InteractiveTail->Tokens[pc->InteractiveTail->NumBytes-TOKEN_DATA_OFFSET]) {
                /* get interactive input */
                if (pc->LexUseStatementPrompt)
                {
                    Prompt = INTERACTIVE_PROMPT_STATEMENT;
                    pc->LexUseStatementPrompt = FALSE;
                }
                else
                    Prompt = INTERACTIVE_PROMPT_LINE;

                if (PlatformGetLine(&LineBuffer[0], LINEBUFFER_MAX, Prompt) == nullptr)
                    return TokenEOF;

                /* put the new line at the end of the linked list of interactive lines */
                LineTokens = LexAnalyse(pc, pc->StrEmpty, &LineBuffer[0], strlen(LineBuffer), &LineBytes);
                LineNode = static_cast<TokenLine *>(VariableAlloc(pc, Parser, sizeof(TokenLine), TRUE));
                LineNode->Tokens = static_cast<unsigned char *>(LineTokens);
                LineNode->NumBytes = LineBytes;
                if (pc->InteractiveHead == nullptr) {
                    /* start a new list */
                    pc->InteractiveHead = LineNode;
                    Parser->Line = 1;
                    Parser->CharacterPos = 0;
                }
                else
                    pc->InteractiveTail->Next = LineNode;

                pc->InteractiveTail = LineNode;
                pc->InteractiveCurrentLine = LineNode;
                Parser->Pos = static_cast<const unsigned char *>(LineTokens);
            }
            else {
                /* go to the next token line */
                if (Parser->Pos != &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes-TOKEN_DATA_OFFSET]) {
                    /* scan for the line */
                    for (pc->InteractiveCurrentLine = pc->InteractiveHead; Parser->Pos != &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes-TOKEN_DATA_OFFSET]; pc->InteractiveCurrentLine = pc->InteractiveCurrentLine->Next)
                    { assert(pc->InteractiveCurrentLine->Next != nullptr); }
                }

                assert(pc->InteractiveCurrentLine != nullptr);
                pc->InteractiveCurrentLine = pc->InteractiveCurrentLine->Next;
                assert(pc->InteractiveCurrentLine != nullptr);
                Parser->Pos = pc->InteractiveCurrentLine->Tokens;
            }

            Token = (LexToken)*(unsigned char *)Parser->Pos;
        }
    } while ((Parser->FileName == pc->StrEmpty && Token == TokenEOF) || Token == TokenEndOfLine);

    Parser->CharacterPos = *((unsigned char *)Parser->Pos + 1);
    ValueSize = LexTokenSize(Token);
    if (ValueSize > 0) {
        /* this token requires a value - unpack it */
        if (Value != nullptr) {
            switch (Token)
            {
                case TokenStringConstant:       pc->LexValue.Typ = pc->CharPtrType; break;
                case TokenIdentifier:           pc->LexValue.Typ = nullptr; break;
                case TokenIntegerConstant:      pc->LexValue.Typ = &pc->LongType; break;
                case TokenUnsignedIntConstanst:      pc->LexValue.Typ = &pc->UnsignedLongType; break;
                case TokenLLConstanst:               pc->LexValue.Typ = &pc->LongLongType; break;
                case TokenUnsignedLLConstanst:       pc->LexValue.Typ = &pc->UnsignedLongLongType; break;
                case TokenCharacterConstant:    pc->LexValue.Typ = &pc->CharType; break;
                case TokenFloatConstant:           pc->LexValue.Typ = &pc->FloatType; break;
                case TokenDoubleConstant:           pc->LexValue.Typ = &pc->DoubleType; break;
                default: break;
            }

            memcpy((void *)pc->LexValue.Val, (void *)((char *)Parser->Pos + TOKEN_DATA_OFFSET), ValueSize);
            pc->LexValue.ValOnHeap = FALSE;
            pc->LexValue.ValOnStack = FALSE;
            pc->LexValue.IsLValue = FALSE;
            pc->LexValue.LValueFrom = nullptr;
            *Value = &pc->LexValue;
        }

        if (IncPos)
            Parser->Pos += ValueSize + TOKEN_DATA_OFFSET;
    }
    else
    {
        if (IncPos && Token != TokenEOF)
            Parser->Pos += TOKEN_DATA_OFFSET;
    }

#ifdef DEBUG_LEXER
    printf("Got token=%02x inc=%d pos=%d\n", Token, IncPos, Parser->CharacterPos);
#endif
    assert(Token >= TokenNone && Token <= TokenEndOfFunction);
    return Token;
}

/* correct the token position depending if we already incremented the position */
void LexHashIncPos(ParseState *Parser, bool IncPos)
{
    if (!IncPos) {
        LexGetRawToken(Parser, nullptr, true);
    }
}

/* handle a #ifdef directive */
void LexHashIfdef(ParseState *Parser, bool IfNot)
{
    /* get symbol to check */
    Value *IdentValue;
    Value *SavedValue;
    int IsDefined;
    LexToken Token = LexGetRawToken(Parser, &IdentValue, true);

    if (Token != TokenIdentifier)
        ProgramFail(Parser, "identifier expected");

    /* is the identifier defined? */
    IsDefined = nitwit::table::TableGet(&Parser->pc->GlobalTable, IdentValue->Val->Identifier, &SavedValue, nullptr, nullptr, nullptr);
    if (Parser->HashIfEvaluateToLevel == Parser->HashIfLevel && ( (IsDefined && !IfNot) || (!IsDefined && IfNot)) )
    {
        /* #if is active, evaluate to this new level */
        Parser->HashIfEvaluateToLevel++;
    }

    Parser->HashIfLevel++;
}

/* handle a #if directive */
void LexHashIf(ParseState *Parser)
{
    /* get symbol to check */
    Value *IdentValue;
    Value *SavedValue = nullptr;
    ParseState MacroParser;
    LexToken Token = LexGetRawToken(Parser, &IdentValue, true);

    if (Token == TokenIdentifier)
    {
        /* look up a value from a macro definition */
        if (!nitwit::table::TableGet(&Parser->pc->GlobalTable, IdentValue->Val->Identifier, &SavedValue, nullptr, nullptr, nullptr))
            ProgramFailWithExitCode(Parser, 244, "'%s' is undefined", IdentValue->Val->Identifier);

        if (SavedValue->Typ->Base != BaseType::TypeMacro)
            ProgramFail(Parser, "value expected");

        nitwit::parse::ParserCopy(&MacroParser, &SavedValue->Val->MacroDef.Body);
        Token = LexGetRawToken(&MacroParser, &IdentValue, true);
    }

    if (!(Token == TokenCharacterConstant || (Token >= TokenIntegerConstant && Token <= TokenUnsignedLLConstanst)))
        ProgramFail(Parser, "value expected");

    /* is the identifier defined? */
    if (Parser->HashIfEvaluateToLevel == Parser->HashIfLevel && IdentValue->Val->Character)
    {
        /* #if is active, evaluate to this new level */
        Parser->HashIfEvaluateToLevel++;
    }

    Parser->HashIfLevel++;
}

/* handle a #else directive */
void LexHashElse(ParseState *Parser)
{
    if (Parser->HashIfEvaluateToLevel == Parser->HashIfLevel - 1)
        Parser->HashIfEvaluateToLevel++;     /* #if was not active, make this next section active */

    else if (Parser->HashIfEvaluateToLevel == Parser->HashIfLevel)
    {
        /* #if was active, now go inactive */
        if (Parser->HashIfLevel == 0)
            ProgramFail(Parser, "#else without #if");

        Parser->HashIfEvaluateToLevel--;
    }
}

/* handle a #endif directive */
void LexHashEndif(ParseState *Parser)
{
    if (Parser->HashIfLevel == 0)
        ProgramFail(Parser, "#endif without #if");

    Parser->HashIfLevel--;
    if (Parser->HashIfEvaluateToLevel > Parser->HashIfLevel)
        Parser->HashIfEvaluateToLevel = Parser->HashIfLevel;
}

#if 0 /* useful for debug */
void LexPrintToken(LexToken Token)
{
    char* TokenNames[] = {
        /* 0x00 */ "None", 
        /* 0x01 */ "Comma",
        /* 0x02 */ "Assign", "AddAssign", "SubtractAssign", "MultiplyAssign", "DivideAssign", "ModulusAssign",
        /* 0x08 */ "ShiftLeftAssign", "ShiftRightAssign", "ArithmeticAndAssign", "ArithmeticOrAssign", "ArithmeticExorAssign",
        /* 0x0d */ "QuestionMark", "Colon", 
        /* 0x0f */ "LogicalOr", 
        /* 0x10 */ "LogicalAnd", 
        /* 0x11 */ "ArithmeticOr", 
        /* 0x12 */ "ArithmeticExor", 
        /* 0x13 */ "Ampersand", 
        /* 0x14 */ "Equal", "NotEqual", 
        /* 0x16 */ "LessThan", "GreaterThan", "LessEqual", "GreaterEqual",
        /* 0x1a */ "ShiftLeft", "ShiftRight", 
        /* 0x1c */ "Plus", "Minus", 
        /* 0x1e */ "Asterisk", "Slash", "Modulus",
        /* 0x21 */ "Increment", "Decrement", "UnaryNot", "UnaryExor", "Sizeof", "Cast",
        /* 0x27 */ "LeftSquareBracket", "RightSquareBracket", "Dot", "Arrow", 
        /* 0x2b */ "OpenBracket", "CloseBracket",
        /* 0x2d */ "Identifier", "IntegerConstant", "FPConstant", "StringConstant", "CharacterConstant",
        /* 0x32 */ "Semicolon", "Ellipsis",
        /* 0x34 */ "LeftBrace", "RightBrace",
        /* 0x36 */ "IntType", "CharType", "FloatType", "DoubleType", "VoidType", "EnumType",
        /* 0x3c */ "LongType", "SignedType", "ShortType", "StaticType", "AutoType", "RegisterType", "ExternType", "StructType", "UnionType", "UnsignedType", "Typedef",
        /* 0x46 */ "Continue", "Do", "Else", "For", "Goto", "If", "While", "Break", "Switch", "Case", "Default", "Return",
        /* 0x52 */ "HashDefine", "HashInclude", "HashIf", "HashIfdef", "HashIfndef", "HashElse", "HashEndif",
        /* 0x59 */ "New", "Delete",
        /* 0x5b */ "OpenMacroBracket",
        /* 0x5c */ "EOF", "EndOfLine", "EndOfFunction"
    };
    printf("{%s}", TokenNames[Token]);
}
#endif

/* get the next token given a parser state, pre-processing as we go */
LexToken LexGetToken(ParseState *Parser, Value **Value, bool IncPos)
{
    LexToken Token;
    int TryNextToken;

    /* implements the pre-processor #if commands */
    do
    {
        int WasPreProcToken = TRUE;

        Token = LexGetRawToken(Parser, Value, IncPos);
        switch (Token)
        {
            case TokenHashIfdef:    LexHashIncPos(Parser, IncPos); LexHashIfdef(Parser, false); break;
            case TokenHashIfndef:   LexHashIncPos(Parser, IncPos); LexHashIfdef(Parser, true); break;
            case TokenHashIf:       LexHashIncPos(Parser, IncPos); LexHashIf(Parser); break;
            case TokenHashElse:     LexHashIncPos(Parser, IncPos); LexHashElse(Parser); break;
            case TokenHashEndif:    LexHashIncPos(Parser, IncPos); LexHashEndif(Parser); break;
            default:                WasPreProcToken = FALSE; break;
        }

        /* if we're going to reject this token, increment the token pointer to the next one */
        TryNextToken = (Parser->HashIfEvaluateToLevel < Parser->HashIfLevel && Token != TokenEOF) || WasPreProcToken;
        if (!IncPos && TryNextToken) {
            LexGetRawToken(Parser, nullptr, true);
        }

    } while (TryNextToken);

    return Token;
}

/* take a quick peek at the next token, skipping any pre-processing */
LexToken LexRawPeekToken(ParseState *Parser)
{
    return (LexToken)*(unsigned char *)Parser->Pos;
}

/* find the end of the line */
void LexToEndOfLine(ParseState *Parser)
{
    while (TRUE)
    {
        LexToken Token = (LexToken)*(unsigned char *)Parser->Pos;
        if (Token == TokenEndOfLine || Token == TokenEOF) {
            return;
        }
        else {
            LexGetRawToken(Parser, nullptr, true);
        }
    }
}

/* copy the tokens from StartParser to EndParser into new memory, removing TokenEOFs and terminate with a TokenEndOfFunction */
void *LexCopyTokens(ParseState *StartParser, ParseState *EndParser)
{
    int MemSize = 0;
    int CopySize;
    unsigned char *Pos = (unsigned char *)StartParser->Pos;
    unsigned char *NewTokens;
    unsigned char *NewTokenPos;
    TokenLine *ILine;
    Picoc *pc = StartParser->pc;

    if (pc->InteractiveHead == nullptr) {
        /* non-interactive mode - copy the tokens */
        MemSize = EndParser->Pos - StartParser->Pos;
        NewTokens = static_cast<unsigned char *>(VariableAlloc(pc, StartParser, MemSize + TOKEN_DATA_OFFSET, TRUE));
        memcpy(NewTokens, (void *)StartParser->Pos, MemSize);
    }
    else {
        /* we're in interactive mode - add up line by line */
        for (pc->InteractiveCurrentLine = pc->InteractiveHead; pc->InteractiveCurrentLine != nullptr && (Pos < &pc->InteractiveCurrentLine->Tokens[0] || Pos >= &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes]); pc->InteractiveCurrentLine = pc->InteractiveCurrentLine->Next)
        {} /* find the line we just counted */

        if (EndParser->Pos >= StartParser->Pos && EndParser->Pos < &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes]) {
            /* all on a single line */
            MemSize = EndParser->Pos - StartParser->Pos;
            NewTokens = static_cast<unsigned char *>(VariableAlloc(pc, StartParser, MemSize + TOKEN_DATA_OFFSET, TRUE));
            memcpy(NewTokens, (void *)StartParser->Pos, MemSize);
        }
        else {
            /* it's spread across multiple lines */
            MemSize = &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes-TOKEN_DATA_OFFSET] - Pos;

            for (ILine = pc->InteractiveCurrentLine->Next; ILine != nullptr && (EndParser->Pos < &ILine->Tokens[0] || EndParser->Pos >= &ILine->Tokens[ILine->NumBytes]); ILine = ILine->Next)
                MemSize += ILine->NumBytes - TOKEN_DATA_OFFSET;

            assert(ILine != nullptr);
            MemSize += EndParser->Pos - &ILine->Tokens[0];
            NewTokens = static_cast<unsigned char *>(VariableAlloc(pc, StartParser, MemSize + TOKEN_DATA_OFFSET, TRUE));

            CopySize = &pc->InteractiveCurrentLine->Tokens[pc->InteractiveCurrentLine->NumBytes-TOKEN_DATA_OFFSET] - Pos;
            memcpy(NewTokens, Pos, CopySize);
            NewTokenPos = NewTokens + CopySize;
            for (ILine = pc->InteractiveCurrentLine->Next; ILine != nullptr && (EndParser->Pos < &ILine->Tokens[0] || EndParser->Pos >= &ILine->Tokens[ILine->NumBytes]); ILine = ILine->Next)
            {
                memcpy(NewTokenPos, &ILine->Tokens[0], ILine->NumBytes - TOKEN_DATA_OFFSET);
                NewTokenPos += ILine->NumBytes-TOKEN_DATA_OFFSET;
            }
            assert(ILine != nullptr);
            memcpy(NewTokenPos, &ILine->Tokens[0], EndParser->Pos - &ILine->Tokens[0]);
        }
    }

    NewTokens[MemSize] = (unsigned char)TokenEndOfFunction;

    return NewTokens;
}

/* indicate that we've completed up to this point in the interactive input and free expired tokens */
void LexInteractiveClear(Picoc *pc, ParseState *Parser)
{
    while (pc->InteractiveHead != nullptr)
    {
        TokenLine *NextLine = pc->InteractiveHead->Next;

        HeapFreeMem(pc, pc->InteractiveHead->Tokens);
        HeapFreeMem(pc, pc->InteractiveHead);
        pc->InteractiveHead = NextLine;
    }

    if (Parser != nullptr)
        Parser->Pos = nullptr;

    pc->InteractiveTail = nullptr;
}

/* indicate that we've completed up to this point in the interactive input and free expired tokens */
void LexInteractiveCompleted(Picoc *pc, ParseState *Parser)
{
    while (pc->InteractiveHead != nullptr && !(Parser->Pos >= &pc->InteractiveHead->Tokens[0] && Parser->Pos < &pc->InteractiveHead->Tokens[pc->InteractiveHead->NumBytes])) {
        /* this token line is no longer needed - free it */
        TokenLine *NextLine = pc->InteractiveHead->Next;

        HeapFreeMem(pc, pc->InteractiveHead->Tokens);
        HeapFreeMem(pc, pc->InteractiveHead);
        pc->InteractiveHead = NextLine;

        if (pc->InteractiveHead == nullptr) {
            /* we've emptied the list */
            Parser->Pos = nullptr;
            pc->InteractiveTail = nullptr;
        }
    }
}

/* the next time we prompt, make it the full statement prompt */
void LexInteractiveStatementPrompt(Picoc *pc)
{
    pc->LexUseStatementPrompt = TRUE;
}


    }
}
