/* picoc parser - parses source and executes statements */

#include "picoc.hpp"
#include "interpreter.hpp"

#include "CoerceT.hpp"

/* for test cases only */
#include <stdio.h>

#ifdef DEBUG_EXPRESSIONS
#define debugf printf
#else
#define debugf(...)
#endif

namespace nitwit {
    namespace parse {

/* deallocate any memory */
void ParseCleanup(Picoc *pc)
{
    while (pc->CleanupTokenList != nullptr)
    {
        struct CleanupTokenNode *Next = pc->CleanupTokenList->Next;

        HeapFreeMem(pc, pc->CleanupTokenList->Tokens);
        if (pc->CleanupTokenList->SourceText != nullptr)
            HeapFreeMem(pc, (void *)pc->CleanupTokenList->SourceText);

        HeapFreeMem(pc, pc->CleanupTokenList);
        pc->CleanupTokenList = Next;
    }
}

/* parse a statement, but only run it if Condition is TRUE */
enum ParseResult ParseStatementMaybeRun(struct ParseState *Parser, int Condition, int CheckTrailingSemicolon)
{
    if (Parser->Mode != RunMode::RunModeSkip && !Condition)
    {
        enum RunMode OldMode = Parser->Mode;
        int Result;
        Parser->Mode = Parser->Mode == RunMode::RunModeGoto ? RunMode::RunModeGoto : RunMode::RunModeSkip;
        Result = ParseStatement(Parser, CheckTrailingSemicolon);
        // the goto could have been resolved now, in that case don't switch to the mode before
        Parser->Mode = OldMode == RunMode::RunModeGoto ? Parser->Mode : OldMode;
        return static_cast<ParseResult>(Result);
    }
    else {
        // save condition branch
        enum ConditionControl OldConditionBranch = Parser->LastConditionBranch;
        enum ParseResult result = ParseStatement(Parser, CheckTrailingSemicolon);
        Parser->LastConditionBranch = OldConditionBranch;
        return result;
    }
}

/* count the number of parameters to a function or macro */
int ParseCountParams(struct ParseState *Parser)
{
    int ParamCount = 0;

    LexToken Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
    if (Token != TokenCloseBracket && Token != TokenEOF)
    {
        /* count the number of parameters */
        ParamCount++;
        int depth = 0;
        while (((Token = nitwit::lex::LexGetToken(Parser, nullptr, true)) != TokenCloseBracket
                || depth != 0) && Token != TokenEOF)
        {
            if (Token == TokenComma && depth == 0)
                ParamCount++;
            if (Token == TokenOpenBracket)
                ++depth;
            if (Token == TokenCloseBracket)
                --depth;
        }
    }

    return ParamCount;
}

/* parse a function definition and store it for later */
Value *ParseFunctionDefinition(ParseState *Parser, ValueType *ReturnType, char *Identifier, bool IsPtrDecl)
{
    ValueType *ParamType;
    char *ParamIdentifier;
    LexToken Token = TokenNone;
    ParseState ParamParser;
    Value *FuncValue;
    Value *OldFuncValue;
    ParseState FuncBody;
    int ParamCount = 0;
    Picoc *pc = Parser->pc;

    if (pc->TopStackFrame != nullptr && !IsPtrDecl)
        ProgramFail(Parser, "nested function definitions are not allowed");
    // track the function
    const char * FunctionBefore = Parser->CurrentFunction;
    Parser->CurrentFunction = Identifier;

    nitwit::lex::LexGetToken(Parser, nullptr, true);  /* open bracket */
    ParserCopy(&ParamParser, Parser);
    ParamCount = ParseCountParams(Parser);
    if (ParamCount > PARAMETER_MAX)
        ProgramFail(Parser, "too many parameters (%d allowed)", PARAMETER_MAX);

    FuncValue = VariableAllocValueAndData(pc, Parser, sizeof(struct FuncDef) + sizeof(struct ValueType *) * ParamCount +
                                                      sizeof(const char *) * ParamCount, IsPtrDecl, nullptr, TRUE, nullptr);
    FuncValue->Typ = &pc->FunctionType;
    FuncValue->Val->FuncDef.ReturnType = ReturnType;
    FuncValue->Val->FuncDef.NumParams = ParamCount;
    FuncValue->Val->FuncDef.VarArgs = FALSE;
    FuncValue->Val->FuncDef.ParamType = (struct ValueType **)((char *)FuncValue->Val + sizeof(struct FuncDef));
    FuncValue->Val->FuncDef.ParamName = (char **)((char *)FuncValue->Val->FuncDef.ParamType + sizeof(struct ValueType *) * ParamCount);

    for (ParamCount = 0; ParamCount < FuncValue->Val->FuncDef.NumParams; ParamCount++)
    {
        /* harvest the parameters into the function definition */
        if (ParamCount == FuncValue->Val->FuncDef.NumParams-1 && nitwit::lex::LexGetToken(&ParamParser, nullptr, false) == TokenEllipsis)
        {
            /* ellipsis at end */
            FuncValue->Val->FuncDef.NumParams--;
            FuncValue->Val->FuncDef.VarArgs = TRUE;
            break;
        }
        else
        {
            int IsConst;
            /* add a parameter */ // fixme: const parameters
            TypeParse(&ParamParser, &ParamType, &ParamIdentifier, nullptr, &IsConst, true);
            if (ParamType->Base == BaseType::TypeVoid)
            {
                /* this isn't a real parameter at all - delete it */
                ParamCount--;
                FuncValue->Val->FuncDef.NumParams--;
            }
            else
            {
                FuncValue->Val->FuncDef.ParamType[ParamCount] = ParamType;
                FuncValue->Val->FuncDef.ParamName[ParamCount] = ParamIdentifier;
            }
        }

        Token = nitwit::lex::LexGetToken(&ParamParser, nullptr, true);
        if (Token != TokenComma && ParamCount < FuncValue->Val->FuncDef.NumParams-1)
            ProgramFail(&ParamParser, "comma expected");
    }

    if (FuncValue->Val->FuncDef.NumParams != 0 && Token != TokenCloseBracket && Token != TokenComma && Token != TokenEllipsis)
        ProgramFail(&ParamParser, "bad parameter");

    if (!IsPtrDecl && strcmp(Identifier, "main") == 0)
    {
        /* make sure it's int main() */
        if ( FuncValue->Val->FuncDef.ReturnType != &pc->IntType &&
             FuncValue->Val->FuncDef.ReturnType != &pc->VoidType )
            ProgramFail(Parser, "main() should return an int or void");

        if (FuncValue->Val->FuncDef.NumParams != 0 &&
             (FuncValue->Val->FuncDef.NumParams != 2 || FuncValue->Val->FuncDef.ParamType[0] != &pc->IntType) )
            ProgramFail(Parser, "bad parameters to main()");
    }

    /* look for a function body */
    Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
    if (Token == TokenSemicolon)
        nitwit::lex::LexGetToken(Parser, nullptr, !IsPtrDecl);    /* it's a prototype or func ptr, absorb the trailing semicolon */
    else if (Token == TokenAttribute){ // it's a GCC attribute property
            do {
                Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
            } while (Token != TokenSemicolon);
        }
    else if (!IsPtrDecl)
    {
        /* it's a full function definition with a body */
        if (Token != TokenLeftBrace)
            ProgramFailWithExitCode(Parser, 249, "bad function definition");

        ParserCopy(&FuncBody, Parser);
        if (ParseStatementMaybeRun(Parser, FALSE, TRUE) != ParseResultOk)
            ProgramFail(Parser, "function definition expected");

        FuncValue->Val->FuncDef.Body = FuncBody;
        FuncValue->Val->FuncDef.Body.Pos = static_cast<const unsigned char *>(nitwit::lex::LexCopyTokens(&FuncBody, Parser));

    }

    char SkipDefinition = FALSE;
    /* is this function already in the global table? */
    if (!IsPtrDecl) {
        if (nitwit::table::TableGet(&pc->GlobalTable, Identifier, &OldFuncValue, nullptr, nullptr, nullptr)) {

            if (OldFuncValue->Val->FuncDef.Body.Pos != nullptr) {
                ProgramFail(Parser, "Function '%s' is already defined", Identifier);
            }

            /* override an old function prototype */
            if (FuncValue->Val->FuncDef.Body.Pos == nullptr &&
                OldFuncValue->Val->FuncDef.Intrinsic != nullptr){
                // just a "non-extern" declaration of library function
                SkipDefinition = TRUE;
                VariableFree(pc, FuncValue);
            } else {
                VariableFree(pc, nitwit::table::TableDelete(pc, &pc->GlobalTable, Identifier));
            }
        }
        if (!SkipDefinition &&
            !nitwit::table::TableSet(pc, &pc->GlobalTable, Identifier, FuncValue, (char*)Parser->FileName, Parser->Line,
                Parser->CharacterPos)) {
            ProgramFail(Parser, "Function '%s' is already defined", Identifier);
        }
    }
    // track the function
    Parser->CurrentFunction = FunctionBefore;
    return FuncValue;
}


/* parse an array initialiser and assign to a variable */
int ParseArrayInitialiser(struct ParseState *Parser, Value *NewVariable, int DoAssignment)
{
    int ArrayIndex = 0;
    enum LexToken Token;
    Value *CValue;

    debugf("Parsing Array Initialiser, DoAssignment = %i, Parser->Mode = %i.\n", DoAssignment, Parser->Mode);
    /* count the number of elements in the array */
    if (DoAssignment && Parser->Mode == RunMode::RunModeRun)
    {
        struct ParseState CountParser;
        int NumElements;

        ParserCopy(&CountParser, Parser);
        NumElements = ParseArrayInitialiser(&CountParser, NewVariable, FALSE);
        debugf("Array Initialiser has %i elements.\n", NumElements);

        if (NewVariable->Typ->Base != BaseType::TypeArray)
            AssignFail(Parser, "%t from array initializer", NewVariable->Typ, nullptr, 0, 0, nullptr, 0);

        if (NewVariable->Typ->ArraySize == 0)
        {
            debugf("Reallocating array with size %i.\n", TypeSizeValue(NewVariable, FALSE));
            NewVariable->Typ = TypeGetMatching(Parser->pc, Parser, NewVariable->Typ->FromType, NewVariable->Typ->Base,
                                               NumElements, NewVariable->Typ->Identifier, TRUE, nullptr);
            VariableRealloc(Parser, NewVariable, TypeSizeValue(NewVariable, FALSE));
        }
        debugf("Array Size: %i\n", NewVariable->Typ->ArraySize);
    }

    /* parse the array initialiser */
    Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
    while (Token != TokenRightBrace)
    {
        if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenLeftBrace)
        {
            /* this is a sub-array initialiser */
            int SubArraySize = 0;
            Value *SubArray = NewVariable;
            if (Parser->Mode == RunMode::RunModeRun && DoAssignment)
            {
                SubArraySize = TypeSize(NewVariable->Typ->FromType, NewVariable->Typ->FromType->ArraySize, TRUE);
                SubArray = VariableAllocValueFromExistingData(Parser, NewVariable->Typ->FromType,
                                                              (union AnyValue *) (&NewVariable->Val->ArrayMem[0] +
                                                                                  SubArraySize * ArrayIndex), TRUE,
                                                              NewVariable, nullptr);
                #ifdef DEBUG_EXPRESSIONS
                int FullArraySize = TypeSize(NewVariable->Typ, NewVariable->Typ->ArraySize, TRUE);
                PRINT_TYPE(NewVariable->Typ);
                debugf("[%d] subarray size: %d (full: %d,%d)\n", ArrayIndex, SubArraySize, FullArraySize, NewVariable->Typ->ArraySize);
                #endif
                if (ArrayIndex >= NewVariable->Typ->ArraySize)
                    ProgramFail(Parser, "too many array elements");
            }
            nitwit::lex::LexGetToken(Parser, nullptr, true);
            ParseArrayInitialiser(Parser, SubArray, DoAssignment);
        }
        else
        {
            Value *ArrayElement = nullptr;

            if (Parser->Mode == RunMode::RunModeRun && DoAssignment)
            {
                struct ValueType * ElementType = NewVariable->Typ;
                int TotalSize = 1;
                int ElementSize = 0;

                /* int x[3][3] = {1,2,3,4} => handle it just like int x[9] = {1,2,3,4} */
                while (ElementType->Base == BaseType::TypeArray)
                {
                    TotalSize *= ElementType->ArraySize;
                    ElementType = ElementType->FromType;

                    /* char x[10][10] = {"abc", "def"} => assign "abc" to x[0], "def" to x[1] etc */
                    if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenStringConstant && ElementType->FromType->Base == BaseType::TypeChar)
                        break;
                }
                ElementSize = TypeSize(ElementType, ElementType->ArraySize, TRUE);
                #ifdef DEBUG_EXPRESSIONS
                debugf("[%d/%d] element size: %d (x%d)\n", ArrayIndex, TotalSize, ElementSize, ElementType->ArraySize);
                #endif
                if (ArrayIndex >= TotalSize)
                    ProgramFail(Parser, "too many array elements");
                ArrayElement = VariableAllocValueFromExistingData(Parser, ElementType,
                                                                  (union AnyValue *) (&NewVariable->Val->ArrayMem[0] +
                                                                                      ElementSize * ArrayIndex), TRUE,
                                                                  NewVariable, nullptr);
                ArrayElement->ArrayRoot = NewVariable;
                ArrayElement->ArrayIndex = ArrayIndex;
            }

            /* this is a normal expression initialiser */
            if (!nitwit::expressions::ExpressionParse(Parser, &CValue))
                ProgramFail(Parser, "expression expected");

            if (Parser->Mode == RunMode::RunModeRun && DoAssignment)
            {
                debugf("About to perform ExpressionAssign() for array entry %i.\n", ArrayIndex);
                nitwit::expressions::ExpressionAssign(Parser, ArrayElement, CValue, FALSE, nullptr, 0, FALSE);
                VariableStackPop(Parser, CValue);
                VariableStackPop(Parser, ArrayElement);
            }
        }

        ArrayIndex++;

        Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
        if (Token == TokenComma)
        {
            nitwit::lex::LexGetToken(Parser, nullptr, true);
            Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
        }
        else if (Token != TokenRightBrace)
            ProgramFail(Parser, "comma expected");
    }

    if (Token == TokenRightBrace)
        nitwit::lex::LexGetToken(Parser, nullptr, true);
    else
        ProgramFail(Parser, "'}' expected");

    return ArrayIndex;
}

/* parse an struct initialiser and assign to a variable */
int ParseSubStructInitialiser(struct ParseState *Parser, Value *NewVariable, Value **StructElement, int DoAssignment)
{
    enum LexToken Token;
    Value *CValue;

    int counter = 0;

    /* parse the sub struct initialiser */
    nitwit::lex::LexGetToken(Parser, nullptr, true);
    Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
    ValueList *SubStructMember = NewVariable->Typ->MemberOrder;

    while (SubStructMember)
    {
        //increase counter of number of subelements in the actual substruct
        counter++;

        // TODO: check for potential subsub elements
        Value *SubMemberValue;
        char *DerefDataLoc = (char *)NewVariable->Val;
            
        if (Parser->Mode == RunMode::RunModeRun && DoAssignment)
        {
            if (SubStructMember == nullptr)
                ProgramFail(Parser, "not that many elements in struct");
                
            /* Get Value from Table by Identifier and parse into MemberValue */ 
            nitwit::table::TableGet(NewVariable->Typ->Members, SubStructMember->Identifier, &SubMemberValue, nullptr, nullptr, nullptr);
            // move to next member
            SubStructMember = SubStructMember->Next; 

            /* make the result value for this member only */
            *StructElement = VariableAllocValueFromExistingData(Parser, SubMemberValue->Typ,
                                                            (AnyValue *) (DerefDataLoc + SubMemberValue->Val->Integer), TRUE,
                                                            NewVariable->LValueFrom, nullptr);
            (*StructElement)->BitField = SubMemberValue->BitField;
            (*StructElement)->ConstQualifier = SubMemberValue->ConstQualifier;
        }

        /* this is a normal expression initialiser */
        if (!nitwit::expressions::ExpressionParse(Parser, &CValue))
            ProgramFail(Parser, "expression expected"); // inc token by 1

        if (Parser->Mode == RunMode::RunModeRun && DoAssignment)
        {
            nitwit::expressions::ExpressionAssign(Parser, *StructElement, CValue, FALSE, nullptr, 0, FALSE);
            VariableStackPop(Parser, CValue);
            VariableStackPop(Parser, *StructElement);
        }
    
        Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
        if (Token == TokenComma)
            nitwit::lex::LexGetToken(Parser, nullptr, true);
        else if (Token != TokenRightBrace)
            ProgramFail(Parser, "comma expected");
    }
    
    if (Token == TokenRightBrace) {
        nitwit::lex::LexGetToken(Parser, nullptr, true);
    } else {
        ProgramFail(Parser, "'}' expected");
    }

    return counter;
}

/* parse an struct initialiser and assign to a variable */
int ParseStructInitialiser(struct ParseState *Parser, Value *NewVariable, int DoAssignment) {
    int ArrayIndex = 0;
    int offsetCounter = 0;
    enum LexToken Token;
    Value *CValue;

    debugf("Parsing Struct Initialiser...\n");
    /* parse the struct initialiser */
    Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
    ValueList *StructMember = NewVariable->Typ->MemberOrder;

    while (StructMember) {
        Value *StructElement = nullptr;

        // check for sub-struct initialiser
        if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenLeftBrace) {
            // parse sub-struct initialiser
            Value *SubMemberValue;
            Value *SubStructElement = nullptr;
            char *DerefDataLoc = (char *) NewVariable->Val;
            int subElementsCounter = 0;

            if (Parser->Mode == RunMode::RunModeRun && DoAssignment) {
                if (StructMember == nullptr)
                    ProgramFail(Parser, "not that many elements in struct");

                // get sub member value and initiliaze sub struct
                nitwit::table::TableGet(NewVariable->Typ->Members, StructMember->Identifier, &SubMemberValue, nullptr, nullptr,
                         nullptr);
                if (SubMemberValue->Typ->Base == BaseType::TypeStruct) {
                    // parse the sub struct
                    subElementsCounter = ParseSubStructInitialiser(Parser, SubMemberValue, &SubStructElement,
                                                                   DoAssignment);
                } else {
                    ParseArrayInitialiser(Parser, SubMemberValue, DoAssignment);
                    SubStructElement = SubMemberValue;
                }

                // move to next member
                StructMember = StructMember->Next;

                StructElement = VariableAllocValueFromExistingData(Parser, SubMemberValue->Typ,
                                                                   (AnyValue *) (DerefDataLoc + 1 +
                                                                                 (sizeof(int) * offsetCounter)),
                                                                   TRUE, NewVariable->LValueFrom, nullptr);

                // increase offset counter by amount of sub struct elements
                offsetCounter += subElementsCounter;

                StructElement->BitField = SubMemberValue->BitField;
                StructElement->ConstQualifier = SubMemberValue->ConstQualifier;
            }

            // assign sub struct element to main struct
            if (Parser->Mode == RunMode::RunModeRun && DoAssignment) {
                nitwit::expressions::ExpressionAssign(Parser, StructElement, SubMemberValue, FALSE, nullptr, 0, FALSE);
                VariableStackPop(Parser, StructElement);
            }
        } else {
            Value *MemberValue;
            char *DerefDataLoc = (char *) NewVariable->Val;

            if (Parser->Mode == RunMode::RunModeRun && DoAssignment) {
                if (StructMember == nullptr)
                    ProgramFail(Parser, "not that many elements in struct");

                /* Get Value from Table by Identifier and parse into MemberValue */
                nitwit::table::TableGet(NewVariable->Typ->Members, StructMember->Identifier, &MemberValue, nullptr, nullptr, nullptr);
                // move to next member
                StructMember = StructMember->Next;

                /* make the result value for this member only */
                if(offsetCounter == 0)
                    StructElement = VariableAllocValueFromExistingData(Parser, MemberValue->Typ,
                                                            (AnyValue *) (DerefDataLoc + MemberValue->Val->Integer),
                                                            TRUE, NewVariable->LValueFrom, nullptr);
                else
                    StructElement = VariableAllocValueFromExistingData(Parser, MemberValue->Typ,
                                                                       (AnyValue *) (DerefDataLoc + (offsetCounter)*MemberValue->Val->Integer),
                                                                       TRUE, NewVariable->LValueFrom, nullptr);

                // increase offset counter by 1 for normal struct elements
                offsetCounter += 1;

                StructElement->BitField = MemberValue->BitField;
                StructElement->ConstQualifier = MemberValue->ConstQualifier;
            }

            /* this is a normal expression initialiser */
            if (!nitwit::expressions::ExpressionParse(Parser, &CValue)) // increases token by 1 step
                ProgramFail(Parser, "expression expected");

            if (Parser->Mode == RunMode::RunModeRun && DoAssignment) {
                nitwit::expressions::ExpressionAssign(Parser, StructElement, CValue, FALSE, nullptr, 0, FALSE);
                VariableStackPop(Parser, CValue);
                VariableStackPop(Parser, StructElement);
            }
        }

        Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
        if (Token == TokenComma) {
            nitwit::lex::LexGetToken(Parser, nullptr, true);
        } else if (Token != TokenRightBrace)
            ProgramFail(Parser, "comma expected");
    }

    if (Token == TokenRightBrace) {
        nitwit::lex::LexGetToken(Parser, nullptr, true);
    } else {
        ProgramFail(Parser, "'}' expected");
    }
    
    return ArrayIndex;
}

/* assign an initial value to a variable */
void ParseDeclarationAssignment(struct ParseState *Parser, Value *NewVariable, int DoAssignment)
{
    Value *CValue;

    if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenLeftBrace)
    {
        /* this is an array or struct initialiser */
        nitwit::lex::LexGetToken(Parser, nullptr, true);
        if (NewVariable && NewVariable->Typ->Base == BaseType::TypeStruct) {
            ParseStructInitialiser(Parser, NewVariable, DoAssignment);
        }
        else {
            ParseArrayInitialiser(Parser, NewVariable, DoAssignment);
        }
    } else {
        /* this is a normal expression initialiser */
        debugf("ParseStatement found a DeclarationAssignment, going into ExpressionParse().\n");
        if (!nitwit::expressions::ExpressionParse(Parser, &CValue))
            ProgramFail(Parser, "expression expected");

        if (Parser->Mode == RunMode::RunModeRun && DoAssignment)
        {
            nitwit::expressions::ExpressionAssign(Parser, NewVariable, CValue, FALSE, nullptr, 0, FALSE);
            VariableStackPop(Parser, CValue);
        }
    }
}

/* declare a variable or function */
int ParseDeclaration(struct ParseState *Parser, LexToken Token, bool& isFunctionDeclaration)
{
    char *Identifier;
    struct ValueType *BasicType;
    struct ValueType *Typ;
    Value *NewVariable = nullptr, *FuncValue = nullptr;
    int IsStatic = FALSE;
    int IsConst = FALSE;
    int FirstVisit = FALSE;
    Picoc *pc = Parser->pc;

    isFunctionDeclaration = false;

    TypeParseFront(Parser, &BasicType, &IsStatic, &IsConst);
    do
    {
        // try parsing a function pointer
        if (!TypeParseFunctionPointer(Parser, BasicType, &Typ, &Identifier, false)){
            TypeParseIdentPart(Parser, BasicType, &Typ, &Identifier, &IsConst);
            if ((Token != TokenVoidType && Token != TokenStructType && Token != TokenUnionType && Token != TokenEnumType) && Identifier == pc->StrEmpty)
                ProgramFail(Parser, "identifier expected");
        }

        if (Identifier != pc->StrEmpty)
        {
            /* handle function definitions */
            if (!(Typ == &pc->FunctionPtrType || (Typ->Base == BaseType::TypeArray && Typ->FromType->Base == BaseType::TypeFunctionPtr))
                && nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenOpenBracket)
            {
                ParseFunctionDefinition(Parser, Typ, Identifier, false);
                isFunctionDeclaration = true;
                return FALSE;
            }
            else
            {
                if (Typ == &pc->VoidType && Identifier != pc->StrEmpty)
                    ProgramFail(Parser, "can't define a void variable");

                if (Typ == &pc->FunctionPtrType || (Typ->Base == BaseType::TypeArray && Typ->FromType->Base == BaseType::TypeFunctionPtr)) {
                    FuncValue = ParseFunctionDefinition(Parser, BasicType, Identifier, true); // fixme Typ should hold the return type
                    isFunctionDeclaration = true;
                }

                if (Parser->Mode == RunMode::RunModeRun || Parser->Mode == RunMode::RunModeGoto){
                    NewVariable = VariableDefineButIgnoreIdentical(Parser, Identifier, Typ, IsStatic, &FirstVisit);
                }
                if (FuncValue != nullptr) VariableFree(pc, FuncValue);

                enum LexToken next_token = nitwit::lex::LexGetToken(Parser, nullptr, false);
                if (next_token == TokenAssign)
                {
                    /* we're assigning an initial value */
                    nitwit::lex::LexGetToken(Parser, nullptr, true);
                    debugf("Found declaration of '%s' with assignment, IsStatic = %i, FirstVisit = %i\n", Identifier, IsStatic, FirstVisit);
                    ParseDeclarationAssignment(Parser, NewVariable, !IsStatic || FirstVisit);
                } else if (NewVariable != nullptr && (next_token == TokenComma || next_token == TokenSemicolon)) {
                    NewVariable->Typ = TypeGetNonDeterministic(Parser, NewVariable->Typ);
#ifdef VERBOSE
                    cw_verbose("Creating new variable %s as NonDet.\n", Identifier);
#endif
                }
                if (NewVariable != nullptr)
                    NewVariable->ConstQualifier = IsConst;
            }
        }

        Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
        if (Token == TokenComma)
            nitwit::lex::LexGetToken(Parser, nullptr, true);

    } while (Token == TokenComma);

    return TRUE;
}

/* parse a #define macro definition and store it for later */
void ParseMacroDefinition(struct ParseState *Parser)
{
    Value *MacroName;
    char *MacroNameStr;
    Value *ParamName;
    Value *MacroValue;

    if (nitwit::lex::LexGetToken(Parser, &MacroName, true) != TokenIdentifier)
        ProgramFail(Parser, "identifier expected");

    MacroNameStr = MacroName->Val->Identifier;

    if (nitwit::lex::LexRawPeekToken(Parser) == TokenOpenMacroBracket)
    {
        /* it's a parameterised macro, read the parameters */
        enum LexToken Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
        struct ParseState ParamParser;
        int NumParams;
        int ParamCount = 0;

        ParserCopy(&ParamParser, Parser);
        NumParams = ParseCountParams(&ParamParser);
        MacroValue = VariableAllocValueAndData(Parser->pc, Parser,
                                               sizeof(struct MacroDef) + sizeof(const char *) * NumParams, FALSE, nullptr,
                                               TRUE, nullptr);
        MacroValue->Val->MacroDef.NumParams = NumParams;
        MacroValue->Val->MacroDef.ParamName = (char **)((char *)MacroValue->Val + sizeof(struct MacroDef));

        Token = nitwit::lex::LexGetToken(Parser, &ParamName, true);

        while (Token == TokenIdentifier)
        {
            /* store a parameter name */
            MacroValue->Val->MacroDef.ParamName[ParamCount++] = ParamName->Val->Identifier;

            /* get the trailing comma */
            Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
            if (Token == TokenComma)
                Token = nitwit::lex::LexGetToken(Parser, &ParamName, true);

            else if (Token != TokenCloseBracket)
                ProgramFail(Parser, "comma expected");
        }

        if (Token != TokenCloseBracket)
            ProgramFail(Parser, "close bracket expected");
    }
    else
    {
        /* allocate a simple unparameterised macro */
        MacroValue = VariableAllocValueAndData(Parser->pc, Parser, sizeof(struct MacroDef), FALSE, nullptr, TRUE, nullptr);
        MacroValue->Val->MacroDef.NumParams = 0;
    }

    /* copy the body of the macro to execute later */
    ParserCopy(&MacroValue->Val->MacroDef.Body, Parser);
    MacroValue->Typ = &Parser->pc->MacroType;
    nitwit::lex::LexToEndOfLine(Parser);
    MacroValue->Val->MacroDef.Body.Pos = static_cast<const unsigned char *>(nitwit::lex::LexCopyTokens(&MacroValue->Val->MacroDef.Body, Parser));

    if (!nitwit::table::TableSet(Parser->pc, &Parser->pc->GlobalTable, MacroNameStr, MacroValue, (char *)Parser->FileName, Parser->Line, Parser->CharacterPos))
        ProgramFail(Parser, "'%s' is already defined", MacroNameStr);
}

/* copy the entire parser state */
void ParserCopy(struct ParseState *To, struct ParseState *From)
{
    memcpy((void *)To, (void *)From, sizeof(*To));
}

/* copy where we're at in the parsing */
void ParserCopyPos(struct ParseState *To, struct ParseState *From)
{
    To->Pos = From->Pos;
    To->Line = From->Line;
    To->HashIfLevel = From->HashIfLevel;
    To->HashIfEvaluateToLevel = From->HashIfEvaluateToLevel;
    To->CharacterPos = From->CharacterPos;
    To->FreshGotoSearch = From->FreshGotoSearch;
    To->SkipIntrinsic = From->SkipIntrinsic;
    To->LastNonDetValue = From->LastNonDetValue;
}

/* parse a "for" statement */
void ParseFor(struct ParseState *Parser)
{
    bool Condition;
    struct ParseState PreConditional;
    struct ParseState PreIncrement;
    struct ParseState PreStatement;
    struct ParseState After;

    enum RunMode OldMode = Parser->Mode;

    int PrevScopeID = 0, ScopeID = VariableScopeBegin(Parser, &PrevScopeID);

    if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenOpenBracket)
        ProgramFail(Parser, "'(' expected");

    if (ParseStatement(Parser, TRUE) != ParseResultOk)
        ProgramFail(Parser, "statement expected");

    ParserCopyPos(&PreConditional, Parser);
    if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenSemicolon) {
        Condition = true;
    } else {
        debugf("ParseStatement found a For, going into ExpressionParse() to parse condition.\n");
        Condition = nitwit::expressions::ExpressionParseLongLong(Parser) != 0;
    }

    ConditionCallback(Parser, Condition);

    if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenSemicolon)
        ProgramFail(Parser, "';' expected");

    ParserCopyPos(&PreIncrement, Parser);
    ParseStatementMaybeRun(Parser, FALSE, FALSE);

    if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenCloseBracket)
        ProgramFail(Parser, "')' expected");

    ParserCopyPos(&PreStatement, Parser);
    if (ParseStatementMaybeRun(Parser, Condition, TRUE) != ParseResultOk)
        ProgramFail(Parser, "statement expected");

    if (Parser->Mode == RunMode::RunModeContinue && OldMode == RunMode::RunModeRun)
        Parser->Mode = RunMode::RunModeRun;

    ParserCopyPos(&After, Parser);

    while (Condition && Parser->Mode == RunMode::RunModeRun)
    {
        ParserCopyPos(Parser, &PreIncrement);
        ParseStatement(Parser, FALSE);

        ParserCopyPos(Parser, &PreConditional);
        if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenSemicolon) {
            Condition = true;
        } else {
            debugf("ParseStatement found a For, going into ExpressionParse() to parse condition.\n");
            Condition = nitwit::expressions::ExpressionParseLongLong(Parser) != 0;
        }

        if (Condition)
        {
            ConditionCallback(Parser, Condition);

            ParserCopyPos(Parser, &PreStatement);
            ParseStatement(Parser, TRUE);

            if (Parser->Mode == RunMode::RunModeContinue)
                Parser->Mode = RunMode::RunModeRun;
        }
    }

    ConditionCallback(Parser, Condition);

    if (Parser->Mode == RunMode::RunModeBreak && OldMode == RunMode::RunModeRun)
        Parser->Mode = RunMode::RunModeRun;

    VariableScopeEnd(Parser, ScopeID, PrevScopeID);

    ParserCopyPos(Parser, &After);
}

void ConditionCallback(struct ParseState *Parser, bool Condition) {
    if (Parser->DebugMode && Parser->Mode == RunMode::RunModeRun) {
        Parser->LastConditionBranch = Condition ? ConditionTrue : ConditionFalse;
        debugf("Parser - Performing debug check for condition.\n");
        DebugCheckStatement(Parser, false, 0);
        Parser->LastConditionBranch = ConditionUndefined;
    }
}

/* parse a block of code and return what mode it returned in */
enum RunMode ParseBlock(struct ParseState *Parser, int AbsorbOpenBrace, int Condition)
{
    int PrevScopeID = 0, ScopeID = VariableScopeBegin(Parser, &PrevScopeID);

    if (AbsorbOpenBrace && nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenLeftBrace)
        ProgramFail(Parser, "'{' expected");

    if (Parser->Mode == RunMode::RunModeSkip || !Condition)
    {
        /* condition failed - skip this block instead */
        enum RunMode OldMode = Parser->Mode;
        Parser->Mode = Parser->Mode == RunMode::RunModeGoto ? RunMode::RunModeGoto : RunMode::RunModeSkip;
        while (ParseStatement(Parser, TRUE) == ParseResultOk)
        {}
        Parser->Mode = OldMode == RunMode::RunModeGoto ? Parser->Mode : OldMode;
    }
    else
    {
        /* just run it in its current mode */
        while (ParseStatement(Parser, TRUE) == ParseResultOk)
        {}
    }

    if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenRightBrace)
        ProgramFail(Parser, "'}' expected");

    VariableScopeEnd(Parser, ScopeID, PrevScopeID);

    return Parser->Mode;
}

/* parse a typedef declaration */
void ParseTypedef(struct ParseState *Parser)
{
    struct ValueType *Typ;
    struct ValueType **TypPtr;
    char *TypeName;
    Value InitValue;
    Value* OldTypedef;

    TypeParse(Parser, &Typ, &TypeName, nullptr, nullptr, 0);

    if (Parser->Mode == RunMode::RunModeRun)
    {
        TypPtr = &Typ;
        InitValue.Typ = &Parser->pc->TypeType;
        InitValue.Val = (union AnyValue *)TypPtr;
        InitValue.IsLValue = FALSE;

        if (VariableDefined(Parser->pc, TypeName)){
            VariableGet(Parser->pc, Parser, TypeName, &OldTypedef);
            if (OldTypedef->Typ == InitValue.Typ){
                return;
            }
        }

        VariableDefine(Parser->pc, Parser, TypeName, &InitValue, nullptr, FALSE, false);
    }
}

char *GetGotoIdentifier(const char *function_id, const char *goto_id) {
    int len = strlen(function_id) + strlen(goto_id) + 1;
    char* s = static_cast<char *>(malloc(sizeof(char) * (len + 1)));
    if (s == nullptr) {
        return nullptr;
    }

    strcpy(s, function_id);
    s[strlen(function_id)] = ':';
    strcpy(s+strlen(function_id) + 1, goto_id);
    return s;
}

/* parse a statement */
enum ParseResult ParseStatement(struct ParseState *Parser, int CheckTrailingSemicolon)
{
    Value *CValue;
    Value *LexerValue;
    Value *VarValue;
    int Condition;
    struct ParseState PreState;
    enum LexToken Token;
    char GotoCallback = FALSE;
    char SkipDebugCheck = FALSE;
    bool isFunctionDeclaration = false;

    /* take note of where we are and then grab a token to see what statement we have */
    ParserCopy(&PreState, Parser);
    Token = nitwit::lex::LexGetToken(Parser, &LexerValue, true);

    struct ParseState ParserPrePosition;
    ParserCopyPos(&ParserPrePosition, Parser);

    /* if we're debugging, check for a breakpoint */
    if (Parser->DebugMode && Parser->Mode == RunMode::RunModeRun){
        switch (Token)
        {
            case TokenIf:
                debugf("Parser - Performing debug check for If.\n");
                DebugCheckStatement(Parser, false, 0);
                break;
            default:
                break;
        }
    }

    int bracket = -1; 
    bool end = false;

    switch (Token)
    {
        case TokenEOF:
            return ParseResultEOF;

        case TokenIdentifier:
            /* might be a typedef-typed variable declaration or it might be an expression */
            if (VariableDefined(Parser->pc, LexerValue->Val->Identifier))
            {
                VariableGet(Parser->pc, Parser, LexerValue->Val->Identifier, &VarValue);
                if (VarValue->Typ->Base == BaseType::Type_Type)
                {
                    *Parser = PreState;
                    CheckTrailingSemicolon = ParseDeclaration(Parser, Token, isFunctionDeclaration);
                    break;
                }
            }
            else
            {
                /* it might be a goto label */
                enum LexToken NextToken = nitwit::lex::LexGetToken(Parser, nullptr, false);
                if (NextToken == TokenColon)
                {
                    /* declare the identifier as a goto label */
                    nitwit::lex::LexGetToken(Parser, nullptr, true);
                    if (Parser->Mode == RunMode::RunModeGoto && LexerValue->Val->Identifier == Parser->SearchGotoLabel) {
                        Parser->Mode = RunMode::RunModeRun;
                        Parser->SearchGotoLabel = nullptr;
                    }

                    return ParseStatement(Parser, TRUE);
//                    CheckTrailingSemicolon = FALSE;
//                    break;
                }
#ifdef FEATURE_AUTO_DECLARE_VARIABLES
                else /* new_identifier = something */
                {    /* try to guess type and declare the variable based on assigned value */
                    if (NextToken == TokenAssign && !VariableDefinedAndOutOfScope(Parser->pc, LexerValue->Val->Identifier))
                    {
                        if (Parser->Mode == RunModeRun)
                        {
                            Value *CValue;
                            char* Identifier = LexerValue->Val->Identifier;

                            nitwit::lex::LexGetToken(Parser, nullptr, true);
                            if (!nitwit::expressions::ExpressionParse(Parser, &CValue))
                            {
                                ProgramFail(Parser, "expected: expression");
                            }
                            
                            #if 0
                            PRINT_SOURCE_POS;
                            PlatformPrintf(Parser->pc->CStdOut, "%t %s = %d;\n", CValue->Typ, Identifier, CValue->Val->Integer);
                            printf("%d\n", VariableDefined(Parser->pc, Identifier));
                            #endif
                            VariableDefine(Parser->pc, Parser, Identifier, CValue, CValue->Typ, TRUE);
                            break;
                        }
                    }
                }
#endif
            }
            /* else fallthrough to expression */
	    /* no break */

        case TokenAsterisk:
        case TokenAmpersand:
        case TokenIncrement:
        case TokenDecrement:
        case TokenOpenBracket:
            *Parser = PreState;
            debugf("ParseStatement found a (, going into ExpressionParse().\n");
            nitwit::expressions::ExpressionParse(Parser, &CValue);
            SkipDebugCheck = TRUE;
            if (Parser->Mode == RunMode::RunModeRun)
                VariableStackPop(Parser, CValue);
            break;

        case TokenLeftBrace:
            ParseBlock(Parser, FALSE, TRUE);
            CheckTrailingSemicolon = FALSE;
            break;

        case TokenIf:
            if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenOpenBracket)
                ProgramFail(Parser, "'(' expected");

            debugf("ParseStatement found an If, going into ExpressionParse().\n");
            Condition = nitwit::expressions::ExpressionParseLongLong(Parser);

            ConditionCallback(Parser, Condition);

            if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenCloseBracket)
                ProgramFail(Parser, "')' expected");

            if (ParseStatementMaybeRun(Parser, Condition, TRUE) != ParseResultOk)
                ProgramFail(Parser, "statement expected");

            if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenElse)
            {
                nitwit::lex::LexGetToken(Parser, nullptr, true);
                if (ParseStatementMaybeRun(Parser,
                        (!Condition && !(PreState.Mode == RunMode::RunModeGoto
                        && Parser->Mode == RunMode::RunModeRun)), TRUE) != ParseResultOk)
                    ProgramFail(Parser, "statement expected");
            }
            CheckTrailingSemicolon = FALSE;
            break;

        case TokenWhile:
            {
                struct ParseState PreConditional;
                enum RunMode PreMode = Parser->Mode;

                if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenOpenBracket)
                    ProgramFail(Parser, "'(' expected");

                ParserCopyPos(&PreConditional, Parser);
                do
                {
                    ParserCopyPos(Parser, &PreConditional);
                    debugf("ParseStatement found a While, going into ExpressionParse() to parse condition.\n");
                    Condition = nitwit::expressions::ExpressionParseLongLong(Parser);
                    ConditionCallback(Parser, Condition);
                    if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenCloseBracket)
                        ProgramFail(Parser, "')' expected");

                    if (ParseStatementMaybeRun(Parser, Condition, TRUE) != ParseResultOk)
                        ProgramFail(Parser, "statement expected");

                    if (Parser->Mode == RunMode::RunModeContinue)
                        Parser->Mode = PreMode == RunMode::RunModeGoto && Parser->SearchGotoLabel == nullptr ? RunMode::RunModeRun : PreMode;

                } while (Parser->Mode == RunMode::RunModeRun && (Condition || (PreMode == RunMode::RunModeGoto && Parser->SearchGotoLabel == nullptr)));

                if (Parser->Mode == RunMode::RunModeBreak)
                    Parser->Mode = PreMode == RunMode::RunModeGoto && Parser->SearchGotoLabel == nullptr ? RunMode::RunModeRun : PreMode;

                CheckTrailingSemicolon = FALSE;
            }
            break;

        case TokenDo:
            {
                struct ParseState PreStatement;
                enum RunMode PreMode = Parser->Mode;
                ParserCopyPos(&PreStatement, Parser);
                do
                {
                    ParserCopyPos(Parser, &PreStatement);
                    if (ParseStatement(Parser, TRUE) != ParseResultOk)
                        ProgramFail(Parser, "statement expected");

                    if (Parser->Mode == RunMode::RunModeContinue)
                        Parser->Mode = PreMode == RunMode::RunModeGoto && Parser->SearchGotoLabel == nullptr ? RunMode::RunModeRun : PreMode;

                    if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenWhile)
                        ProgramFail(Parser, "'while' expected");

                    if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenOpenBracket)
                        ProgramFail(Parser, "'(' expected");

                    debugf("ParseStatement found a Do, going into ExpressionParse() to parse condition.\n");
                    Condition = nitwit::expressions::ExpressionParseLongLong(Parser);
                    ConditionCallback(Parser, Condition);
                    if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenCloseBracket)
                        ProgramFail(Parser, "')' expected");

                } while (Condition && Parser->Mode == RunMode::RunModeRun);

                if (Parser->Mode == RunMode::RunModeBreak)
                    Parser->Mode = PreMode == RunMode::RunModeGoto && Parser->SearchGotoLabel == nullptr ? RunMode::RunModeRun : PreMode;
            }
            break;

        case TokenFor:
            ParseFor(Parser);
            CheckTrailingSemicolon = FALSE;
            break;

        case TokenSemicolon:
            CheckTrailingSemicolon = FALSE;
            break;

        case TokenConst:
        case TokenIntType:
        case TokenShortType:
        case TokenCharType:
        case TokenLongType:
        case TokenFloatType:
        case TokenDoubleType:
        case TokenVoidType:
        case TokenStructType:
        case TokenUnionType:
        case TokenEnumType:
        case TokenSignedType:
        case TokenUnsignedType:
        case TokenStaticType:
        case TokenAutoType:
        case TokenRegisterType:
#ifdef NO_HEADER_INCLUDE
        case TokenExternType:
#endif
            *Parser = PreState;
            CheckTrailingSemicolon = ParseDeclaration(Parser, Token, isFunctionDeclaration);
            break;
#ifndef NO_HEADER_INCLUDE
        case TokenExternType:
            // just ignore the externs...
            for (Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
                ;
                Token = nitwit::lex::LexGetToken(Parser, nullptr, true)) {
                if (Token == TokenOpenBracket) {
                    if (bracket == -1) bracket = 0; // enable ignoring till end of block
                    bracket++;
                } else if (Token == TokenCloseBracket) {
                    bracket--;
                    if (bracket == 0) end = true; // finished block
                }
                else if (bracket == -1 && Token == TokenSemicolon) end = true; // use ; when not block to stop ignore
                if (end || Token == TokenEOF)
                    break;
            }
            CheckTrailingSemicolon = FALSE;
            break;
#endif
        case TokenHashDefine:
            ParseMacroDefinition(Parser);
            CheckTrailingSemicolon = FALSE;
            break;

#ifndef NO_HASH_INCLUDE
        case TokenHashInclude:
            if (nitwit::lex::LexGetToken(Parser, &LexerValue, true) != TokenStringConstant)
                ProgramFail(Parser, "\"filename.h\" expected");

            IncludeFile(Parser->pc, (char *)LexerValue->Val->Pointer);
            CheckTrailingSemicolon = FALSE;
            break;
#endif

        case TokenSwitch:
            if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenOpenBracket)
                ProgramFail(Parser, "'(' expected");

            debugf("ParseStatement found a Switch, going into ExpressionParse() to parse condition.\n");
            Condition = nitwit::expressions::ExpressionParseLongLong(Parser);

            if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenCloseBracket)
                ProgramFail(Parser, "')' expected");

            if (nitwit::lex::LexGetToken(Parser, nullptr, false) != TokenLeftBrace)
                ProgramFail(Parser, "'{' expected");

            {
                /* new block so we can store parser state */
                enum RunMode OldMode = Parser->Mode;
                int OldSearchLabel = Parser->SearchLabel;
                Parser->Mode = RunMode::RunModeCaseSearch;
                Parser->SearchLabel = Condition;

                ParseBlock(Parser, TRUE, (OldMode != RunMode::RunModeSkip) && (OldMode != RunMode::RunModeReturn));

                if (Parser->Mode != RunMode::RunModeReturn)
                    Parser->Mode = OldMode;

                Parser->SearchLabel = OldSearchLabel;
            }

            CheckTrailingSemicolon = FALSE;
            break;

        case TokenCase:
            if (Parser->Mode == RunMode::RunModeCaseSearch)
            {
                Parser->Mode = RunMode::RunModeRun;
                debugf("ParseStatement found a Case, going into ExpressionParse() to parse condition.\n");
                Condition = nitwit::expressions::ExpressionParseLongLong(Parser);
                Parser->Mode = RunMode::RunModeCaseSearch;
            }
            else {
                debugf("ParseStatement found a Case, going into ExpressionParse() to parse condition.\n");
                Condition = nitwit::expressions::ExpressionParseLongLong(Parser);
            }

            if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenColon)
                ProgramFail(Parser, "':' expected");

            if (Parser->Mode == RunMode::RunModeCaseSearch && Condition == Parser->SearchLabel)
                Parser->Mode = RunMode::RunModeRun;

            CheckTrailingSemicolon = FALSE;
            break;

        case TokenDefault:
            if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenColon)
                ProgramFail(Parser, "':' expected");

            if (Parser->Mode == RunMode::RunModeCaseSearch)
                Parser->Mode = RunMode::RunModeRun;

            CheckTrailingSemicolon = FALSE;
            break;

        case TokenBreak:
            if (Parser->Mode == RunMode::RunModeRun)
                Parser->Mode = RunMode::RunModeBreak;
            break;

        case TokenContinue:
            if (Parser->Mode == RunMode::RunModeRun)
                Parser->Mode = RunMode::RunModeContinue;
            break;

        case TokenReturn:
            if (Parser->Mode == RunMode::RunModeRun)
            {
                // returning from this function;
                const char *RetBeforeName = Parser->ReturnFromFunction;
                Parser->ReturnFromFunction = Parser->pc->TopStackFrame->FuncName;
                if (!Parser->pc->TopStackFrame || Parser->pc->TopStackFrame->ReturnValue->Typ->Base != BaseType::TypeVoid)
                {
                    debugf("ParseStatement found a Return, going into ExpressionParse().\n");
                    if (!nitwit::expressions::ExpressionParse(Parser, &CValue)) {
                        ProgramFail(Parser, "value required in return");
                    }

                    if (!Parser->pc->TopStackFrame) { /* return from top-level program? */
                        PlatformExit(Parser->pc, CoerceT<long long>(CValue));
                    }
                    else {
                        nitwit::expressions::ExpressionAssign(Parser, Parser->pc->TopStackFrame->ReturnValue, CValue, TRUE, nullptr, 0, FALSE);
                    }
                    VariableStackPop(Parser, CValue);
                }
                else
                {
                    debugf("ParseStatement found a Return, going into ExpressionParse().\n");
                    if (nitwit::expressions::ExpressionParse(Parser, &CValue))
                        ProgramFail(Parser, "value in return from a void function");
                }
                Parser->ReturnFromFunction = RetBeforeName;

                Parser->Mode = RunMode::RunModeReturn;
            }
            else {
                debugf("ParseStatement found a Return, going into ExpressionParse().\n");
                nitwit::expressions::ExpressionParse(Parser, &CValue);
            }
            break;

        case TokenTypedef:
            ParseTypedef(Parser);
            break;

        case TokenGoto:
            if (nitwit::lex::LexGetToken(Parser, &LexerValue, true) != TokenIdentifier)
                ProgramFail(Parser, "identifier expected");

            if (Parser->Mode == RunMode::RunModeRun)
            {
                /* start scanning for the goto label */
                Parser->SearchGotoLabel = LexerValue->Val->Identifier;
                Parser->Mode = RunMode::RunModeGoto;
                Parser->FreshGotoSearch = TRUE;
                GotoCallback = TRUE;
            }
            break;

        case TokenDelete:
        {
            /* try it as a function or variable name to delete */
            if (nitwit::lex::LexGetToken(Parser, &LexerValue, true) != TokenIdentifier)
                ProgramFail(Parser, "identifier expected");

            if (Parser->Mode == RunMode::RunModeRun)
            {
                /* delete this variable or function */
                CValue = nitwit::table::TableDelete(Parser->pc, &Parser->pc->GlobalTable, LexerValue->Val->Identifier);

                if (CValue == nullptr)
                    ProgramFailWithExitCode(Parser, 244, "'%s' is not defined", LexerValue->Val->Identifier);

                VariableFree(Parser->pc, CValue);
            }
            break;
        }

        default:
            *Parser = PreState;
            return ParseResultError;
    }
    
    /* check if a semicolon is expected and if it occured */ 
    if (CheckTrailingSemicolon)
    {
        if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenSemicolon)
            ProgramFail(Parser, "';' expected");
    }

    /* if we're debugging, check for a breakpoint */
    if ((Parser->DebugMode && Parser->Mode == RunMode::RunModeRun && !SkipDebugCheck) || GotoCallback){
        struct ParseState NowPosition;
        ParserCopyPos(&NowPosition, Parser);
        ParserCopyPos(Parser, &ParserPrePosition);
#ifdef DEBUG_WITNESS_EDGES
        debugf("About to go into debugger - token is %s, position now is %zu, position before was %zu.\n", tokenToString(Token), NowPosition.Line, ParserPrePosition.Line);
#endif
        switch (Token)
        {
            case TokenGoto:
            case TokenSwitch:
            case TokenCase:
            case TokenDefault:
            case TokenContinue:
            case TokenBreak:
            case TokenReturn:
                debugf("Parser - Performing debug check for control flow modifier.\n");
                DebugCheckStatement(Parser, false, 0);
                break;
            case TokenTypedef:
            case TokenIdentifier:
            case TokenConst:
            case TokenIntType:
            case TokenShortType:
            case TokenCharType:
            case TokenLongType:
            case TokenFloatType:
            case TokenDoubleType:
            case TokenVoidType:
            case TokenStructType:
            case TokenUnionType:
            case TokenEnumType:
            case TokenSignedType:
            case TokenUnsignedType:
            case TokenStaticType:
            case TokenAutoType:
            case TokenRegisterType:
            case TokenExternType:
                debugf("Parser - Performing debug check for type declaration.\n");
                DebugCheckStatement(Parser, !isFunctionDeclaration, isFunctionDeclaration ? 0 : NowPosition.Line);
                break;
            default:
                break;
        }
        ParserCopyPos(Parser, &NowPosition);
    }

    return ParseResultOk;
}

/* quick scan a source file for definitions */
void PicocParse(Picoc *pc, const char *FileName, const char *Source, int SourceLen, int RunIt, int CleanupNow,
                int CleanupSource, int EnableDebugger, void (*DebuggerCallback)(ParseState*, bool, std::size_t const&))
{
    ParseState Parser;
    ParseResult Ok;
    CleanupTokenNode *NewCleanupNode;
    char *RegFileName = nitwit::table::TableStrRegister(pc, FileName);

    void *Tokens = nitwit::lex::LexAnalyse(pc, RegFileName, Source, SourceLen, nullptr);

    /* allocate a cleanup node so we can clean up the tokens later */
    if (!CleanupNow)
    {
        NewCleanupNode = static_cast<CleanupTokenNode *>(HeapAllocMem(pc, sizeof(struct CleanupTokenNode)));
        if (NewCleanupNode == nullptr)
            ProgramFailNoParserWithExitCode(pc, 251, "Out of memory");

        NewCleanupNode->Tokens = Tokens;
        if (CleanupSource)
            NewCleanupNode->SourceText = Source;
        else
            NewCleanupNode->SourceText = nullptr;

        NewCleanupNode->Next = pc->CleanupTokenList;
        pc->CleanupTokenList = NewCleanupNode;
    }

    /* initialize the Parser */
    nitwit::lex::LexInitParser(&Parser, pc, Source, Tokens, RegFileName, RunIt, EnableDebugger, DebuggerCallback);

    /* do the parsing */
    do {
        Ok = ParseStatement(&Parser, TRUE);
    } while (Ok == ParseResultOk);

    if (Ok == ParseResultError)
        ProgramFail(&Parser, "parse error");

    /* clean up */
    if (CleanupNow)
        HeapFreeMem(pc, Tokens);
}

/* parse interactively */
void PicocParseInteractiveNoStartPrompt(Picoc *pc, int EnableDebugger)
{
    ParseState Parser;
    ParseResult Ok;

    nitwit::lex::LexInitParser(&Parser, pc, nullptr, nullptr, pc->StrEmpty, TRUE, EnableDebugger, nullptr);
    PicocPlatformSetExitPoint(pc);
    nitwit::lex::LexInteractiveClear(pc, &Parser);

    do
    {
        nitwit::lex::LexInteractiveStatementPrompt(pc);
        Ok = ParseStatement(&Parser, TRUE);
        nitwit::lex::LexInteractiveCompleted(pc, &Parser);

    } while (Ok == ParseResultOk);

    if (Ok == ParseResultError)
        ProgramFail(&Parser, "parse error");

    PlatformPrintf(pc->CStdOut, "\n");
}

/* parse interactively, showing a startup message */
void PicocParseInteractive(Picoc *pc)
{
    PlatformPrintf(pc->CStdOut, INTERACTIVE_PROMPT_START);
    PicocParseInteractiveNoStartPrompt(pc, TRUE);
}


    }
}
