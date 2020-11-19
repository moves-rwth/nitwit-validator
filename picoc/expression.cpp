/* picoc expression evaluator - a stack-based expression evaluation system
 * which handles operator precedence */

#include "interpreter.hpp"

/* whether evaluation is left to right for a given precedence level */
#define IS_LEFT_TO_RIGHT(p) ((p) != 2 && (p) != 14)
#define BRACKET_PRECEDENCE 20

/* If the destination is not float, we can't assign a floating value to it, we need to convert it to integer instead */
#define ASSIGN_FP_OR_INT(value) \
        if (IS_FP(BottomValue)) { ResultFP = AssignFP(Parser, BottomValue, value); } \
        else { ResultLLInt = AssignLongLong(Parser, BottomValue, (long)(value), FALSE); ResultIsInt = TRUE; } \

#define DEEP_PRECEDENCE (BRACKET_PRECEDENCE*1000)
#ifdef DEBUG_EXPRESSIONS
#define debugf printf
#else
void debugf(char *Format, ...)
{
}
#endif
/* local prototypes */
enum OperatorOrder
{
    OrderNone,
    OrderPrefix,
    OrderInfix,
    OrderPostfix
};

/* a stack of expressions we use in evaluation */
struct ExpressionStack
{
    struct ExpressionStack *Next;       /* the next lower item on the stack */
    Value *Val;                  /* the value for this stack node */
    enum LexToken Op;                   /* the operator */
    short unsigned int Precedence;      /* the operator precedence of this node */
    unsigned char Order;                /* the evaluation order of this operator */
};

/* operator precedence definitions */
struct OpPrecedence
{
    unsigned int PrefixPrecedence:4;
    unsigned int PostfixPrecedence:4;
    unsigned int InfixPrecedence:4;
    char *Name;
};

/* NOTE: the order of this array must correspond exactly to the order of these tokens in enum LexToken */
static struct OpPrecedence OperatorPrecedence[] =
{
    /* TokenNone, */ { 0, 0, 0, "none" },
    /* TokenComma, */ { 0, 0, 0, "," },
    /* TokenAssign, */ { 0, 0, 2, "=" }, /* TokenAddAssign, */ { 0, 0, 2, "+=" }, /* TokenSubtractAssign, */ { 0, 0, 2, "-=" },
    /* TokenMultiplyAssign, */ { 0, 0, 2, "*=" }, /* TokenDivideAssign, */ { 0, 0, 2, "/=" }, /* TokenModulusAssign, */ { 0, 0, 2, "%=" },
    /* TokenShiftLeftAssign, */ { 0, 0, 2, "<<=" }, /* TokenShiftRightAssign, */ { 0, 0, 2, ">>=" }, /* TokenArithmeticAndAssign, */ { 0, 0, 2, "&=" },
    /* TokenArithmeticOrAssign, */ { 0, 0, 2, "|=" }, /* TokenArithmeticExorAssign, */ { 0, 0, 2, "^=" },
    /* TokenQuestionMark, */ { 0, 3, 0, "?" }, /* TokenColon, */ { 0, 0, 3, ":" },
    /* TokenLogicalOr, */ { 0, 0, 4, "||" },
    /* TokenLogicalAnd, */ { 0, 0, 5, "&&" },
    /* TokenArithmeticOr, */ { 0, 0, 6, "|" },
    /* TokenArithmeticExor, */ { 0, 0, 7, "^" },
    /* TokenAmpersand, */ { 14, 0, 8, "&" },
    /* TokenEqual, */  { 0, 0, 9, "==" }, /* TokenNotEqual, */ { 0, 0, 9, "!=" },
    /* TokenLessThan, */ { 0, 0, 10, "<" }, /* TokenGreaterThan, */ { 0, 0, 10, ">" }, /* TokenLessEqual, */ { 0, 0, 10, "<=" }, /* TokenGreaterEqual, */ { 0, 0, 10, ">=" },
    /* TokenShiftLeft, */ { 0, 0, 11, "<<" }, /* TokenShiftRight, */ { 0, 0, 11, ">>" },
    /* TokenPlus, */ { 14, 0, 12, "+" }, /* TokenMinus, */ { 14, 0, 12, "-" },
    /* TokenAsterisk, */ { 14, 0, 13, "*" }, /* TokenSlash, */ { 0, 0, 13, "/" }, /* TokenModulus, */ { 0, 0, 13, "%" },
    /* TokenIncrement, */ { 14, 15, 0, "++" }, /* TokenDecrement, */ { 14, 15, 0, "--" }, /* TokenUnaryNot, */ { 14, 0, 0, "!" }, /* TokenUnaryExor, */ { 14, 0, 0, "~" }, /* TokenSizeof, */ { 14, 0, 0, "sizeof" }, /* TokenCast, */ { 14, 0, 0, "cast" },
    /* TokenLeftSquareBracket, */ { 0, 0, 15, "[" }, /* TokenRightSquareBracket, */ { 0, 15, 0, "]" }, /* TokenDot, */ { 0, 0, 15, "." }, /* TokenArrow, */ { 0, 0, 15, "->" },
    /* TokenOpenBracket, */ { 15, 0, 15, "(" }, /* TokenCloseBracket, */ { 0, 15, 0, ")" }
};

void ExpressionParseFunctionCall(struct ParseState *Parser, struct ExpressionStack **StackTop, const char *FuncName, int RunIt);

#ifdef DEBUG_EXPRESSIONS
/* show the contents of the expression stack */
void ExpressionStackShow(Picoc *pc, struct ExpressionStack *StackTop)
{
    printf("Expression stack [0x%lx,0x%lx]: ", (long)pc->HeapStackTop, (long)StackTop);
    
    while (StackTop != nullptr)
    {
        if (StackTop->Order == OrderNone)
        { 
            /* it's a value */
            if (StackTop->Val->IsLValue)
                printf("lvalue=");
            else
                printf("value=");
                
            switch (StackTop->Val->Typ->Base)
            {
                case TypeVoid:      printf("void"); break;
                case TypeInt:       printf("%d:int", StackTop->Val->Val->Integer); break;
                case TypeShort:     printf("%d:short", StackTop->Val->Val->ShortInteger); break;
                case TypeChar:      printf("%d:char", StackTop->Val->Val->Character); break;
                case TypeLong:      printf("%ld:long", StackTop->Val->Val->LongInteger); break;
                case TypeUnsignedShort: printf("%d:unsigned short", StackTop->Val->Val->UnsignedShortInteger); break;
                case TypeUnsignedInt: printf("%d:unsigned int", StackTop->Val->Val->UnsignedInteger); break;
                case TypeUnsignedLong: printf("%ld:unsigned long", StackTop->Val->Val->UnsignedLongInteger); break;
                case TypeDouble:        printf("%f:fp", StackTop->Val->Val->Double); break;
                case TypeFunction:  printf("%s:function", StackTop->Val->Val->Identifier); break;
                case TypeMacro:     printf("%s:macro", StackTop->Val->Val->Identifier); break;
                case TypePointer:
                    if (StackTop->Val->Val->Pointer == nullptr)
                        printf("ptr(nullptr)");
                    else if (StackTop->Val->Typ->FromType->Base == TypeChar)
                        printf("\"%s\":string", (char *)StackTop->Val->Val->Pointer);
                    else
                        printf("ptr(0x%lx)", (long)StackTop->Val->Val->Pointer); 
                    break;
                case TypeArray:     printf("array"); break;
                case TypeStruct:    printf("%s:struct", StackTop->Val->Val->Identifier); break;
                case TypeUnion:     printf("%s:union", StackTop->Val->Val->Identifier); break;
                case TypeEnum:      printf("%s:enum", StackTop->Val->Val->Identifier); break;
                case Type_Type:     PrintType(StackTop->Val->Val->Typ, pc->CStdOut); printf(":type"); break;
                default:            printf("unknown"); break;
            }
            printf("[0x%lx,0x%lx]", (long)StackTop, (long)StackTop->Val);
        }
        else
        { 
            /* it's an operator */
            printf("op='%s' %s %d", OperatorPrecedence[(int)StackTop->Op].Name, 
                (StackTop->Order == OrderPrefix) ? "prefix" : ((StackTop->Order == OrderPostfix) ? "postfix" : "infix"), 
                StackTop->Precedence);
            printf("[0x%lx]", (long)StackTop);
        }
        
        StackTop = StackTop->Next;
        if (StackTop != nullptr)
            printf(", ");
    }
    
    printf("\n");
}
#endif

int IsTypeToken(struct ParseState *Parser, enum LexToken t, Value *LexValue)
{
    if (t >= TokenIntType && t <= TokenUnsignedType)
        return 1; /* base type */

    /* typedef'ed type? */
    if (t == TokenIdentifier) /* see TypeParseFront, case TokenIdentifier and ParseTypedef */
    {
        Value * VarValue;
        if (VariableDefined(Parser->pc,(const char*) LexValue->Val->Pointer))
        {
            VariableGet(Parser->pc, Parser, (const char*)LexValue->Val->Pointer, &VarValue);
            if (VarValue->Typ == &Parser->pc->TypeType)
                return 1;
        }
    }

    return 0;
}

/* push a node on to the expression stack */
void ExpressionStackPushValueNode(struct ParseState *Parser, struct ExpressionStack **StackTop, Value *ValueLoc)
{
    auto *StackNode = static_cast<ExpressionStack *>(VariableAlloc(Parser->pc, Parser,
                                                                                     sizeof(struct ExpressionStack),
                                                                                     FALSE));
    StackNode->Next = *StackTop;
    StackNode->Val = ValueLoc;
    *StackTop = StackNode;
#ifdef FANCY_ERROR_MESSAGES
    StackNode->Line = Parser->Line;
    StackNode->CharacterPos = Parser->CharacterPos;
#endif
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
}

/* push a blank value on to the expression stack by type */
Value *ExpressionStackPushValueByType(struct ParseState *Parser, struct ExpressionStack **StackTop, struct ValueType *PushType)
{
    Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, PushType, FALSE, nullptr, FALSE);
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);

    return ValueLoc;
}

/* push a value on to the expression stack */
void ExpressionStackPushValue(struct ParseState *Parser, struct ExpressionStack **StackTop, Value *PushValue)
{
    Value *ValueLoc = VariableAllocValueAndCopy(Parser->pc, Parser, PushValue, FALSE);
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

void ExpressionStackPushLValue(struct ParseState *Parser, struct ExpressionStack **StackTop, Value *PushValue, int Offset)
{
    Value *ValueLoc = VariableAllocValueShared(Parser, PushValue);
    ValueLoc->Val = static_cast<AnyValue *>((void *) ((char *) ValueLoc->Val + Offset));
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

void ExpressionStackPushDereference(struct ParseState *Parser, struct ExpressionStack **StackTop, Value *DereferenceValue)
{
    Value *DerefVal;
    Value *ValueLoc;
    int Offset;
    struct ValueType *DerefType;
    int DerefIsLValue;
    void *DerefDataLoc = VariableDereferencePointer(Parser, DereferenceValue, &DerefVal, &Offset, &DerefType, &DerefIsLValue);
//    if (DerefDataLoc == nullptr)
//        ProgramFail(Parser, "nullptr pointer dereference");

    ValueLoc = VariableAllocValueFromExistingData(Parser, DerefType, (union AnyValue *) DerefDataLoc, DerefIsLValue,
                                                  DerefVal, nullptr);
    ValueLoc->ConstQualifier = DereferenceValue->ConstQualifier;

    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

void ExpressionPushLongLong(struct ParseState *Parser, struct ExpressionStack **StackTop, long long IntValue)
{
    Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->LongLongType, FALSE, nullptr, FALSE);
    ValueLoc->Val->LongLongInteger = IntValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

void ExpressionPushUnsignedLongLong(struct ParseState *Parser, struct ExpressionStack **StackTop, unsigned long long IntValue)
{
    Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->UnsignedLongLongType, FALSE, nullptr, FALSE);
    ValueLoc->Val->UnsignedLongLongInteger = IntValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}


void ExpressionPushInt(struct ParseState *Parser, struct ExpressionStack **StackTop, long IntValue)
{
    struct Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->LongType, FALSE, nullptr, FALSE);
    ValueLoc->Val->LongInteger = IntValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

void ExpressionPushUnsignedInt(struct ParseState *Parser, struct ExpressionStack **StackTop, unsigned long IntValue)
{
    struct Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->UnsignedLongType, FALSE, nullptr, FALSE);
    ValueLoc->Val->UnsignedLongInteger = IntValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}



#ifndef NO_FP
void ExpressionPushDouble(struct ParseState *Parser, struct ExpressionStack **StackTop, double FPValue)
{
    Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->DoubleType, FALSE, nullptr, FALSE);
    ValueLoc->Val->Double = FPValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

void ExpressionPushFloat(struct ParseState *Parser, struct ExpressionStack **StackTop, float FPValue)
{
    Value *ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->FloatType, FALSE, nullptr, FALSE);
    ValueLoc->Val->Float = FPValue;
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}
#endif

/* assign to a pointer */
void ExpressionAssignToPointer(struct ParseState *Parser, Value *ToValue, Value *FromValue, const char *FuncName, int ParamNo, int AllowPointerCoercion)
{
    struct ValueType *PointedToType = ToValue->Typ->FromType;

    if (FromValue->Typ->Base == ToValue->Typ->Base || FromValue->Typ == Parser->pc->VoidPtrType
        || (ToValue->Typ == Parser->pc->VoidPtrType && FromValue->Typ->Base == TypePointer)
//        || (FromValue->Typ->FromType != nullptr && TypeIsNonDeterministic(FromValue->Typ->FromType)
//            && ToValue->Typ->FromType->Base == FromValue->Typ->FromType->Base && FromValue->Typ->Base != TypeArray)
        )
    {
        ToValue->Val->Pointer = FromValue->Val->Pointer;      /* plain old pointer assignment */
    }

    else if (FromValue->Typ->Base == TypeArray && (PointedToType->Base == FromValue->Typ->FromType->Base || ToValue->Typ == Parser->pc->VoidPtrType))
    {
        /* the form is: blah *x = array of blah */
        ToValue->Val->Pointer = (void *)&FromValue->Val->ArrayMem[0];
    }
    else if (FromValue->Typ->Base == TypePointer && FromValue->Typ->FromType->Base == TypeArray &&
               (PointedToType == FromValue->Typ->FromType->FromType || ToValue->Typ == Parser->pc->VoidPtrType) )
    {
        /* the form is: blah *x = pointer to array of blah */
        ToValue->Val->Pointer = VariableDereferencePointer(Parser, FromValue, nullptr, nullptr, nullptr, nullptr);
    }
    else if (IS_NUMERIC_COERCIBLE(FromValue) && CoerceInteger(FromValue) == 0)
    {
        /* null pointer assignment */
        ToValue->Val->Pointer = nullptr;
    }
    else if (AllowPointerCoercion && IS_NUMERIC_COERCIBLE(FromValue))
    {
        /* assign integer to native pointer */
        ToValue->Val->Pointer = (void *)(unsigned long) CoerceUnsignedInteger(FromValue);
    }
    else if (AllowPointerCoercion && FromValue->Typ->Base == TypePointer)
    {
        /* assign a pointer to a pointer to a different type */
        ToValue->Val->Pointer = FromValue->Val->Pointer;
    }
    else
        AssignFail(Parser, "%t from %t", ToValue->Typ, FromValue->Typ, 0, 0, FuncName, ParamNo);
}

/* assign any kind of value */
void ExpressionAssign(struct ParseState *Parser, Value *DestValue, Value *SourceValue, int Force, const char *FuncName, int ParamNo, int AllowPointerCoercion)
{
    if (!DestValue->IsLValue && !Force)
        AssignFail(Parser, "not an lvalue", nullptr, nullptr, 0, 0, FuncName, ParamNo);

    if (IS_NUMERIC_COERCIBLE(DestValue) && !IS_NUMERIC_COERCIBLE_PLUS_POINTERS(SourceValue, AllowPointerCoercion))
        AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

    // if Source is NonDet, so should the assigned value
    if (TypeIsNonDeterministic(SourceValue->Typ)) {
        DestValue->Typ = TypeGetNonDeterministic(Parser, DestValue->Typ);
    }

    if (DestValue->LValueFrom != nullptr){
        if (TypeIsNonDeterministic(SourceValue->Typ)) {
            DestValue->LValueFrom->Typ = TypeGetNonDeterministic(Parser, DestValue->LValueFrom->Typ);
        } else {
            DestValue->LValueFrom->Typ = TypeGetDeterministic(Parser, DestValue->LValueFrom->Typ);
        }
    }

    // check if not const
    if (DestValue->ConstQualifier == TRUE){
        switch (DestValue->Typ->Base){
            case TypeInt:
            case TypeShort:
            case TypeChar:
            case TypeLong:
            case TypeLongLong:
            case TypeUnsignedInt:
            case TypeUnsignedShort:
            case TypeUnsignedLong:
            case TypeUnsignedLongLong:
            case TypeUnsignedChar:
            case TypeDouble:
            case TypeStruct:
            case TypeUnion:
                AssignFail(Parser, "const %t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);
                break;
            default:
                break;
        }
    }

    // assign SourceValue by DestType
    switch (DestValue->Typ->Base)
    {
        case TypeInt:              DestValue->Val->Integer = CoerceInteger(SourceValue); break;
        case TypeShort:            DestValue->Val->ShortInteger = (short) CoerceInteger(SourceValue); break;
        case TypeChar:             DestValue->Val->Character = (char) CoerceInteger(SourceValue); break;
        case TypeLong:             DestValue->Val->LongInteger = CoerceInteger(SourceValue); break;
        case TypeUnsignedInt:      DestValue->Val->UnsignedInteger = CoerceUnsignedInteger(SourceValue); break;
        case TypeUnsignedShort:    DestValue->Val->UnsignedShortInteger = (unsigned short) CoerceUnsignedInteger(
                    SourceValue); break;
        case TypeUnsignedLong:     DestValue->Val->UnsignedLongInteger = CoerceUnsignedInteger(SourceValue); break;
        case TypeUnsignedChar:     DestValue->Val->UnsignedCharacter = (unsigned char) CoerceUnsignedInteger(
                    SourceValue); break;

        case TypeLongLong:         DestValue->Val->LongLongInteger = CoerceLongLong(SourceValue); break;
        case TypeUnsignedLongLong: DestValue->Val->UnsignedLongLongInteger = CoerceUnsignedLongLong(
                    SourceValue); break;
#ifndef NO_FP
        case TypeDouble:
            if (!IS_NUMERIC_COERCIBLE_PLUS_POINTERS(SourceValue, AllowPointerCoercion))
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

            DestValue->Val->Double = CoerceDouble(SourceValue);
            break;
        case TypeFloat:
            if (!IS_NUMERIC_COERCIBLE_PLUS_POINTERS(SourceValue, AllowPointerCoercion))
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

            DestValue->Val->Float = CoerceFloat(SourceValue);
            break;
#endif
        case TypePointer:
            ExpressionAssignToPointer(Parser, DestValue, SourceValue, FuncName, ParamNo, AllowPointerCoercion);
            break;

        case TypeArray:
            if (SourceValue->Typ->Base == TypeArray && DestValue->Typ->ArraySize == 0)
            {
                /* destination array is unsized - need to resize the destination array to the same size as the source array */
                DestValue->Typ = SourceValue->Typ;
                VariableRealloc(Parser, DestValue, TypeSizeValue(DestValue, FALSE));

                if (DestValue->LValueFrom != nullptr)
                {
                    /* copy the resized value back to the LValue */
                    DestValue->LValueFrom->Val = DestValue->Val;
                    DestValue->LValueFrom->AnyValOnHeap = DestValue->AnyValOnHeap;
                }
            }

            /* char array = "abcd" */
            if (DestValue->Typ->FromType->Base == TypeChar && SourceValue->Typ->Base == TypePointer && SourceValue->Typ->FromType->Base == TypeChar)
            {
                if (DestValue->Typ->ArraySize == 0) /* char x[] = "abcd", x is unsized */
                {
                    int Size = strlen((const char*)SourceValue->Val->Pointer) + 1;
                    
                    #ifdef DEBUG_ARRAY_INITIALIZER
                    PRINT_SOURCE_POS;
                    fprintf(stderr, "str size: %d\n", Size);
                    #endif
                    
                    DestValue->Typ = TypeGetMatching(Parser->pc, Parser, DestValue->Typ->FromType, DestValue->Typ->Base,
                                                     Size, DestValue->Typ->Identifier, TRUE, nullptr);
                    VariableRealloc(Parser, DestValue, TypeSizeValue(DestValue, FALSE));
                }
                /* else, it's char x[10] = "abcd" */

                #ifdef DEBUG_ARRAY_INITIALIZER
                PRINT_SOURCE_POS;
                fprintf(stderr, "char[%d] from char* (len=%d)\n", DestValue->Typ->ArraySize, strlen(SourceValue->Val->Pointer));
                #endif
                memcpy((void *)DestValue->Val, SourceValue->Val->Pointer, TypeSizeValue(DestValue, FALSE));
                break;
            }

            if (DestValue->Typ->Base != SourceValue->Typ->Base
                && DestValue->Typ->FromType->Base != SourceValue->Typ->FromType->Base) // FIXME : comparing array type is not safe this way
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

            if (DestValue->Typ->ArraySize != SourceValue->Typ->ArraySize)
                AssignFail(Parser, "from an array of size %d to one of size %d", nullptr, nullptr, DestValue->Typ->ArraySize, SourceValue->Typ->ArraySize, FuncName, ParamNo);

//            memcpy((void *)DestValue->Val, (void *)SourceValue->Val, TypeSizeValue(DestValue, FALSE));
            // Instead of copying just point to same memory. Like in C!
            DestValue->Val = SourceValue->Val;
            break;

        case TypeStruct:
        case TypeUnion:
            if (DestValue->Typ != SourceValue->Typ)
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

            memcpy((void *)DestValue->Val, (void *)SourceValue->Val, TypeSizeValue(SourceValue, FALSE));
            break;
        case TypeFunctionPtr:
            if (IS_NUMERIC_COERCIBLE(SourceValue) && CoerceLongLong(SourceValue) == 0){
                DestValue->Val->Identifier = nullptr;
                break;
            }
            if (DestValue->Typ != SourceValue->Typ)
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);

            DestValue->Val->Identifier = SourceValue->Val->Identifier;
            break;
        default:
            AssignFail(Parser, "%t", DestValue->Typ, nullptr, 0, 0, FuncName, ParamNo);
            break;
    }

    AdjustBitField(Parser, DestValue);
}

/* evaluate the first half of a ternary operator x ? y : z */
void ExpressionQuestionMarkOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, Value *TopValue)
{
    if (!IS_NUMERIC_COERCIBLE(TopValue))
        ProgramFail(Parser, "first argument to '?' should be a number");

    if (CoerceInteger(TopValue))
    {
        /* the condition's true, return the BottomValue */
        ConditionCallback(Parser, TRUE);
        ExpressionStackPushValue(Parser, StackTop, TopValue);
    }
    else
    {
        /* the condition's false, return void */
        ConditionCallback(Parser, FALSE);
        ExpressionStackPushValueByType(Parser, StackTop, &Parser->pc->VoidType);
    }
}

/* evaluate the second half of a ternary operator x ? y : z */
void ExpressionColonOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, Value *BottomValue, Value *TopValue)
{
    if (TopValue->Typ->Base == TypeVoid)
    {
        /* invoke the "else" part - return the BottomValue */
        ExpressionStackPushValue(Parser, StackTop, BottomValue);
    }
    else
    {
        /* it was a "then" - return the TopValue */
        ExpressionStackPushValue(Parser, StackTop, TopValue);
    }
}

/* evaluate a prefix operator */
void ExpressionPrefixOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Op, Value *TopValue)
{
    Value *Result;
    union AnyValue *ValPtr;

    debugf("ExpressionPrefixOperator()\n");
    switch (Op)
    {
        case TokenAmpersand:
            if (!TopValue->IsLValue)
                ProgramFail(Parser, "can't get the address of this");

            if (TopValue->Typ->Base == TypeFunctionPtr) {
                char * id = TopValue->Val->Identifier;
                Result = VariableAllocValueFromType(Parser->pc, Parser, TopValue->Typ, FALSE, nullptr, FALSE);
                Result->Val->Identifier = id;
            } else {
                ValPtr = TopValue->Val;
                Result = VariableAllocValueFromType(Parser->pc, Parser,
                                                    TypeGetMatching(Parser->pc, Parser, TopValue->Typ, TypePointer, 0,
                                                                    Parser->pc->StrEmpty, TRUE,
                                                                    nullptr),
                        FALSE, TopValue->LValueFrom, FALSE);
                Result->Val->Pointer = (void *)ValPtr;
            }
            ExpressionStackPushValueNode(Parser, StackTop, Result);
            break;

        case TokenAsterisk:
            if (TopValue->Typ->Base == TypeFunctionPtr || (TopValue->Typ->Base == TypeArray
                    && TopValue->Typ->FromType->Base == TypeFunctionPtr)) {
                ExpressionStackPushValue(Parser, StackTop, TopValue);
                break;
            }
            ExpressionStackPushDereference(Parser, StackTop, TopValue);
            break;

        case TokenSizeof:
            /* return the size of the argument */
            if (TopValue->Typ == &Parser->pc->TypeType)
                ExpressionPushLongLong(Parser, StackTop,
                                       TypeSize(TopValue->Val->Typ, TopValue->Val->Typ->ArraySize, TRUE));
            else
                ExpressionPushLongLong(Parser, StackTop, TypeSize(TopValue->Typ, TopValue->Typ->ArraySize, TRUE));
            break;

        default:
            /* an arithmetic operator */
#ifndef NO_FP
            if (TopValue->Typ->Base == TypeDouble)
            {
                /* floating point prefix arithmetic */
                double ResultFP = 0.0;

                switch (Op)
                {
                    case TokenPlus:         ResultFP = TopValue->Val->Double; break;
                    case TokenMinus:        ResultFP = -TopValue->Val->Double; break;
                    case TokenIncrement:    ResultFP = AssignFP(Parser, TopValue, TopValue->Val->Double + 1); break;
                    case TokenDecrement:    ResultFP = AssignFP(Parser, TopValue, TopValue->Val->Double - 1); break;
                    case TokenUnaryNot:     ResultFP = !TopValue->Val->Double; break;
                    default:                ProgramFail(Parser, "invalid operation"); break;
                }

                ExpressionPushDouble(Parser, StackTop, ResultFP);
            }
            else if (TopValue->Typ->Base == TypeFloat)
            {
                /* floating point prefix arithmetic */
                float ResultFP = 0.0;

                switch (Op)
                {
                    case TokenPlus:         ResultFP = TopValue->Val->Float; break;
                    case TokenMinus:        ResultFP = -TopValue->Val->Float; break;
                    case TokenIncrement:    ResultFP = AssignFP(Parser, TopValue, TopValue->Val->Float + 1); break;
                    case TokenDecrement:    ResultFP = AssignFP(Parser, TopValue, TopValue->Val->Float - 1); break;
                    case TokenUnaryNot:     ResultFP = !TopValue->Val->Float; break;
                    default:                ProgramFail(Parser, "invalid operation"); break;
                }

                ExpressionPushFloat(Parser, StackTop, ResultFP);
            }
            else
#endif
            if (IS_NUMERIC_COERCIBLE(TopValue) && !IS_UNSIGNED(TopValue))
            {
                /* integer prefix arithmetic */
                long long ResultInt = 0;
                long long TopInt = CoerceInteger(TopValue);
                switch (Op)
                {
                    case TokenPlus:         ResultInt = TopInt; break;
                    case TokenMinus:        ResultInt = -TopInt; break;
                    case TokenIncrement:    ResultInt = AssignLongLong(Parser, TopValue, TopInt + 1, FALSE); break;
                    case TokenDecrement:    ResultInt = AssignLongLong(Parser, TopValue, TopInt - 1, FALSE); break;
                    case TokenUnaryNot:     ResultInt = !TopInt; break;
                    case TokenUnaryExor:    ResultInt = ~TopInt; break;
                    default:                ProgramFail(Parser, "invalid operation"); break;
                }

                ExpressionPushInt(Parser, StackTop, ResultInt);
            }
            else if (IS_NUMERIC_COERCIBLE(TopValue) && IS_UNSIGNED(TopValue)) // todo unsigned assign+push
            {
                /* integer prefix arithmetic */
                unsigned long long ResultInt = 0;
                unsigned long long TopInt = CoerceUnsignedLongLong(TopValue);
                switch (Op)
                {
                    case TokenPlus:         ResultInt = TopInt; break;
                    case TokenMinus:        ResultInt = -TopInt; break;
                    case TokenIncrement:    ResultInt = AssignLongLong(Parser, TopValue, TopInt + 1, FALSE); break;
                    case TokenDecrement:    ResultInt = AssignLongLong(Parser, TopValue, TopInt - 1, FALSE); break;
                    case TokenUnaryNot:     ResultInt = !TopInt; break;
                    case TokenUnaryExor:    ResultInt = ~TopInt; break;
                    default:                ProgramFail(Parser, "invalid operation"); break;
                }

                ExpressionPushLongLong(Parser, StackTop, ResultInt);
            }
            else if (TopValue->Typ->Base == TypePointer)
            {
                /* pointer prefix arithmetic */
                int Size = TypeSize(TopValue->Typ->FromType, 0, TRUE);
                Value *StackValue;
                void *ResultPtr = nullptr;

                if (TopValue->Val->Pointer == nullptr && Op != TokenUnaryNot)
                    ProgramFail(Parser, "invalid use of a nullptr pointer");

                if (!TopValue->IsLValue)
                    ProgramFail(Parser, "can't assign to this");

                switch (Op)
                {
                    case TokenIncrement:    TopValue->Val->Pointer = (void *)((char *)TopValue->Val->Pointer + Size);
                                            ResultPtr = TopValue->Val->Pointer; break;
                    case TokenDecrement:    TopValue->Val->Pointer = (void *)((char *)TopValue->Val->Pointer - Size);
                                            ResultPtr = TopValue->Val->Pointer; break;
                    case TokenUnaryNot:     ResultPtr = reinterpret_cast<void *>(!TopValue->Val->Pointer); break;
                    default:                ProgramFail(Parser, "invalid operation"); break;
                }

//                ResultPtr = TopValue->Val->Pointer;
                StackValue = ExpressionStackPushValueByType(Parser, StackTop, TopValue->Typ);
                StackValue->Val->Pointer = ResultPtr;
            }
            else
                ProgramFail(Parser, "invalid operation");
            break;
    }
}

/* evaluate a postfix operator */
void ExpressionPostfixOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Op, Value *TopValue)
{
    debugf("ExpressionPostfixOperator()\n");
    if (Op == TokenQuestionMark)
        ExpressionQuestionMarkOperator(Parser, StackTop, TopValue);
    else
#ifndef NO_FP
    if (TopValue->Typ->Base == TypeDouble)
    {
        /* floating point prefix arithmetic */
        double ResultFP = 0.0;

        switch (Op)
        {
            case TokenIncrement:    ResultFP = AssignFP(Parser, TopValue, TopValue->Val->Double + 1); break;
            case TokenDecrement:    ResultFP = AssignFP(Parser, TopValue, TopValue->Val->Double - 1); break;
            default:                ProgramFail(Parser, "invalid operation"); break;
        }

        ExpressionPushDouble(Parser, StackTop, ResultFP);
    }
    else if (TopValue->Typ->Base == TypeFloat)
    {
        /* floating point prefix arithmetic */
        double ResultFP = 0.0;

        switch (Op)
        {
            case TokenIncrement:    ResultFP = AssignFP(Parser, TopValue, TopValue->Val->Float + 1); break;
            case TokenDecrement:    ResultFP = AssignFP(Parser, TopValue, TopValue->Val->Float - 1); break;
            default:                ProgramFail(Parser, "invalid operation"); break;
        }

        ExpressionPushDouble(Parser, StackTop, ResultFP);
    }
    else
#endif
    if (IS_NUMERIC_COERCIBLE(TopValue) && !IS_UNSIGNED(TopValue))
    {
        unsigned long long ResultInt = 0;
        unsigned long long TopInt = CoerceUnsignedLongLong(TopValue);
        switch (Op)
        {
            case TokenIncrement:            ResultInt = AssignLongLong(Parser, TopValue, TopInt + 1, TRUE); break;
            case TokenDecrement:            ResultInt = AssignLongLong(Parser, TopValue, TopInt - 1, TRUE); break;
            case TokenRightSquareBracket:   ProgramFail(Parser, "not supported"); break;  /* XXX */
            case TokenCloseBracket:         ProgramFail(Parser, "not supported"); break;  /* XXX */
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }

        ExpressionPushLongLong(Parser, StackTop, ResultInt);
    }
    else if (IS_NUMERIC_COERCIBLE(TopValue))
    {
        long long ResultInt = 0;
        long long TopInt = CoerceLongLong(TopValue);
        switch (Op)
        {
            case TokenIncrement:            ResultInt = AssignLongLong(Parser, TopValue, TopInt + 1, TRUE); break;
            case TokenDecrement:            ResultInt = AssignLongLong(Parser, TopValue, TopInt - 1, TRUE); break;
            case TokenRightSquareBracket:   ProgramFail(Parser, "not supported"); break;  /* XXX */
            case TokenCloseBracket:         ProgramFail(Parser, "not supported"); break;  /* XXX */
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }

        ExpressionPushLongLong(Parser, StackTop, ResultInt);
    }
    else if (TopValue->Typ->Base == TypePointer)
    {
        /* pointer postfix arithmetic */
        int Size = TypeSize(TopValue->Typ->FromType, 0, TRUE);
        Value *StackValue;
        void *OrigPointer = TopValue->Val->Pointer;

        if (TopValue->Val->Pointer == nullptr)
            ProgramFail(Parser, "invalid use of a nullptr pointer");

        if (!TopValue->IsLValue)
            ProgramFail(Parser, "can't assign to this");

        switch (Op)
        {
            case TokenIncrement:    TopValue->Val->Pointer = (void *)((char *)TopValue->Val->Pointer + Size); break;
            case TokenDecrement:    TopValue->Val->Pointer = (void *)((char *)TopValue->Val->Pointer - Size); break;
            default:                ProgramFail(Parser, "invalid operation"); break;
        }

        StackValue = ExpressionStackPushValueByType(Parser, StackTop, TopValue->Typ);
        StackValue->Val->Pointer = OrigPointer;
    }
    else
        ProgramFail(Parser, "invalid operation");
}

/* evaluate an infix operator */
void ExpressionInfixOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Op, Value *BottomValue, Value *TopValue)
{
    long long ResultLLInt = 0;
    Value *StackValue;
    void *Pointer;

    debugf("ExpressionInfixOperator()\n");
    if (BottomValue == nullptr || TopValue == nullptr)
        ProgramFail(Parser, "invalid expression");

    if (Op == TokenLeftSquareBracket)
    {
        /* array index */
        int ArrayIndex;
        Value *Result = nullptr;

        if (!IS_NUMERIC_COERCIBLE(TopValue))
            ProgramFail(Parser, "array index must be an integer");

        ArrayIndex = CoerceLongLong(TopValue);

        // set nondet value for BottomValue when it is a nondet array
        if(BottomValue->Typ->NDList != NULL) {
            BottomValue->Typ->IsNonDet = getNonDetListElement(BottomValue->Typ->NDList, ArrayIndex);
        }

        /* make the array element result */
        switch (BottomValue->Typ->Base)
        {
            case TypeArray:
                Result = VariableAllocValueFromExistingData(Parser,
                                  TypeIsNonDeterministic(BottomValue->Typ) ? TypeGetNonDeterministic(Parser, BottomValue->Typ->FromType) : BottomValue->Typ->FromType,
                                  (union AnyValue *) (
                                          &BottomValue->Val->ArrayMem[0] +
                                          TypeSize(BottomValue->Typ, ArrayIndex,
                                                   TRUE)),
                                  BottomValue->IsLValue,
                                  BottomValue->LValueFrom, nullptr); break;

            case TypePointer: Result = VariableAllocValueFromExistingData(Parser, BottomValue->Typ->FromType,
                                  (union AnyValue *) (
                                          (char *) BottomValue->Val->Pointer +
                                          TypeSize(BottomValue->Typ->FromType,
                                                   0, TRUE) * ArrayIndex),
                                  BottomValue->LValueFrom != nullptr ? TRUE: BottomValue->IsLValue,
                                  BottomValue->LValueFrom, nullptr); break;
                                  // allow to change to LValue later ((unsigned*)&dmax)[0] = 0x47efffff;
            default:          ProgramFail(Parser, "this %t is not an array", BottomValue->Typ);
        }

        ExpressionStackPushValueNode(Parser, StackTop, Result);
    }

    else if (Op == TokenColon)
        ExpressionColonOperator(Parser, StackTop, TopValue, BottomValue);

#ifndef NO_FP
    else if ( (IS_FP(TopValue) && IS_FP(BottomValue)) ||
              (IS_FP(TopValue) && IS_NUMERIC_COERCIBLE(BottomValue)) ||
              (IS_NUMERIC_COERCIBLE(TopValue) && IS_FP(BottomValue)) )
    {
        /* floating point infix arithmetic */
        int ResultIsInt = FALSE;
        double ResultFP = 0.0;
        double TopFP = (IS_FP(TopValue)) ? CoerceDouble(TopValue) : (double) CoerceLongLong(TopValue);
        double BottomFP = (IS_FP(BottomValue)) ? CoerceDouble(BottomValue) : (double) CoerceLongLong(
                BottomValue);

        if (BottomValue->IsLValue == TRUE && BottomValue->LValueFrom != nullptr && Op >= TokenAssign && Op <= TokenModulusAssign){
            if (TypeIsNonDeterministic(TopValue->Typ))
                BottomValue->LValueFrom->Typ = TypeGetNonDeterministic(Parser, BottomValue->LValueFrom->Typ);
            else if (TypeIsNonDeterministic(BottomValue->Typ)) //topvalue is deterministic, but don't change structs/unions
                BottomValue->LValueFrom->Typ = TypeGetDeterministic(Parser, BottomValue->LValueFrom->Typ);
        }
        switch (Op)
        {
            case TokenAssign:               ASSIGN_FP_OR_INT(TopFP); break;
            case TokenAddAssign:            ASSIGN_FP_OR_INT(BottomFP + TopFP); break;
            case TokenSubtractAssign:       ASSIGN_FP_OR_INT(BottomFP - TopFP); break;
            case TokenMultiplyAssign:       ASSIGN_FP_OR_INT(BottomFP * TopFP); break;
            case TokenDivideAssign:         ASSIGN_FP_OR_INT(BottomFP / TopFP); break;
            case TokenEqual:                ResultLLInt = BottomFP == TopFP; ResultIsInt = TRUE; break;
            case TokenNotEqual:             ResultLLInt = BottomFP != TopFP; ResultIsInt = TRUE; break;
            case TokenLessThan:             ResultLLInt = BottomFP < TopFP; ResultIsInt = TRUE; break;
            case TokenGreaterThan:          ResultLLInt = BottomFP > TopFP; ResultIsInt = TRUE; break;
            case TokenLessEqual:            ResultLLInt = BottomFP <= TopFP; ResultIsInt = TRUE; break;
            case TokenGreaterEqual:         ResultLLInt = BottomFP >= TopFP; ResultIsInt = TRUE; break;
            case TokenPlus:                 ResultFP = BottomFP + TopFP; break;
            case TokenMinus:                ResultFP = BottomFP - TopFP; break;
            case TokenAsterisk:             ResultFP = BottomFP * TopFP; break;
            case TokenSlash:                ResultFP = BottomFP / TopFP; break;
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }

        if (ResultIsInt)
            ExpressionPushLongLong(Parser, StackTop, ResultLLInt);
        else
            ExpressionPushDouble(Parser, StackTop, ResultFP);
    }
#endif
    else if (IS_NUMERIC_COERCIBLE(TopValue) && IS_NUMERIC_COERCIBLE(BottomValue) && !IS_UNSIGNED(TopValue) && !IS_UNSIGNED(BottomValue))
    {
        /* integer operation */     // todo same in assumptions
        long long TopInt = CoerceLongLong(TopValue);
        long long BottomInt = CoerceLongLong(BottomValue);
        if (TypeIsNonDeterministic(TopValue->Typ))
            if (BottomValue->IsLValue == TRUE && BottomValue->LValueFrom != nullptr)
                BottomValue->LValueFrom->Typ = TypeGetNonDeterministic(Parser, BottomValue->LValueFrom->Typ);
        switch (Op)
        {
            case TokenAssign:               ResultLLInt = AssignLongLong(Parser, BottomValue, TopInt, FALSE); break;
            case TokenAddAssign:            ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt + TopInt,
                                                                  FALSE); break;
            case TokenSubtractAssign:       ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt - TopInt,
                                                                  FALSE); break;
            case TokenMultiplyAssign:       ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt * TopInt,
                                                                  FALSE); break;
            case TokenDivideAssign:         ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt / TopInt,
                                                                  FALSE); break;
#ifndef NO_MODULUS
            case TokenModulusAssign:        ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt % TopInt,
                                                                  FALSE); break;
#endif
            case TokenShiftLeftAssign:      ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt << TopInt, FALSE); break;
            case TokenShiftRightAssign:     ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt >> TopInt, FALSE); break;
            case TokenArithmeticAndAssign:  ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt & TopInt,
                                                                  FALSE); break;
            case TokenArithmeticOrAssign:   ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt | TopInt,
                                                                  FALSE); break;
            case TokenArithmeticExorAssign: ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                  BottomInt ^ TopInt,
                                                                  FALSE); break;
            case TokenLogicalOr:            ResultLLInt = BottomInt || TopInt; break;
            case TokenLogicalAnd:           ResultLLInt = BottomInt && TopInt; break;
            case TokenArithmeticOr:         ResultLLInt = BottomInt | TopInt; break;
            case TokenArithmeticExor:       ResultLLInt = BottomInt ^ TopInt; break;
            case TokenAmpersand:            ResultLLInt = BottomInt & TopInt; break;
            case TokenEqual:                ResultLLInt = BottomInt == TopInt; break;
            case TokenNotEqual:             ResultLLInt = BottomInt != TopInt; break;
            case TokenLessThan:             ResultLLInt = BottomInt < TopInt; break;
            case TokenGreaterThan:          ResultLLInt = BottomInt > TopInt; break;
            case TokenLessEqual:            ResultLLInt = BottomInt <= TopInt; break;
            case TokenGreaterEqual:         ResultLLInt = BottomInt >= TopInt; break;
            case TokenShiftLeft:            ResultLLInt = BottomInt << TopInt; break;
            case TokenShiftRight:           ResultLLInt = BottomInt >> TopInt; break;
            case TokenPlus:                 ResultLLInt = BottomInt + TopInt; break;
            case TokenMinus:                ResultLLInt = BottomInt - TopInt; break;
            case TokenAsterisk:             ResultLLInt = BottomInt * TopInt; break;
            case TokenSlash:                ResultLLInt = BottomInt / TopInt; break;
#ifndef NO_MODULUS
            case TokenModulus:              ResultLLInt = BottomInt % TopInt; break;
#endif
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }

        if (TopValue->Typ->Base == TypeLongLong ||
            TopValue->Typ->Base == TypeUnsignedLongLong ||
            BottomValue->Typ->Base == TypeLongLong ||
            BottomValue->Typ->Base == TypeUnsignedLongLong){
            ExpressionPushLongLong(Parser, StackTop, ResultLLInt);
        } else {
            ExpressionPushInt(Parser, StackTop, ResultLLInt);
        }
    }
    else if (IS_NUMERIC_COERCIBLE(TopValue) && IS_NUMERIC_COERCIBLE(BottomValue))
    {
        /* integer operation */     // todo same in assumptions
        unsigned long long TopInt = CoerceUnsignedLongLong(TopValue);
        unsigned long long BottomInt = CoerceUnsignedLongLong(BottomValue);
        if (TypeIsNonDeterministic(TopValue->Typ))
            if (BottomValue->IsLValue == TRUE && BottomValue->LValueFrom != nullptr)
                BottomValue->LValueFrom->Typ = TypeGetNonDeterministic(Parser, BottomValue->LValueFrom->Typ);
        switch (Op)
        {
            case TokenAssign:               ResultLLInt = AssignLongLong(Parser, BottomValue, TopInt, FALSE); break;
            case TokenAddAssign:            ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt + TopInt,
                                                                         FALSE); break;
            case TokenSubtractAssign:       ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt - TopInt,
                                                                         FALSE); break;
            case TokenMultiplyAssign:       ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt * TopInt,
                                                                         FALSE); break;
            case TokenDivideAssign:         ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt / TopInt,
                                                                         FALSE); break;
#ifndef NO_MODULUS
            case TokenModulusAssign:        ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt % TopInt,
                                                                         FALSE); break;
#endif
            case TokenShiftLeftAssign:      ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt << TopInt, FALSE); break;
            case TokenShiftRightAssign:     ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt >> TopInt, FALSE); break;
            case TokenArithmeticAndAssign:  ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt & TopInt,
                                                                         FALSE); break;
            case TokenArithmeticOrAssign:   ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt | TopInt,
                                                                         FALSE); break;
            case TokenArithmeticExorAssign: ResultLLInt = AssignLongLong(Parser, BottomValue,
                                                                         BottomInt ^ TopInt,
                                                                         FALSE); break;
            case TokenLogicalOr:            ResultLLInt = BottomInt || TopInt; break;
            case TokenLogicalAnd:           ResultLLInt = BottomInt && TopInt; break;
            case TokenArithmeticOr:         ResultLLInt = BottomInt | TopInt; break;
            case TokenArithmeticExor:       ResultLLInt = BottomInt ^ TopInt; break;
            case TokenAmpersand:            ResultLLInt = BottomInt & TopInt; break;
            case TokenEqual:                ResultLLInt = BottomInt == TopInt; break;
            case TokenNotEqual:             ResultLLInt = BottomInt != TopInt; break;
            case TokenLessThan:             ResultLLInt = BottomInt < TopInt; break;
            case TokenGreaterThan:          ResultLLInt = BottomInt > TopInt; break;
            case TokenLessEqual:            ResultLLInt = BottomInt <= TopInt; break;
            case TokenGreaterEqual:         ResultLLInt = BottomInt >= TopInt; break;
            case TokenShiftLeft:            ResultLLInt = BottomInt << TopInt; break;
            case TokenShiftRight:           ResultLLInt = BottomInt >> TopInt; break;
            case TokenPlus:                 ResultLLInt = BottomInt + TopInt; break;
            case TokenMinus:                ResultLLInt = BottomInt - TopInt; break;
            case TokenAsterisk:             ResultLLInt = BottomInt * TopInt; break;
            case TokenSlash:                ResultLLInt = BottomInt / TopInt; break;
#ifndef NO_MODULUS
            case TokenModulus:              ResultLLInt = BottomInt % TopInt; break;
#endif
            default:                        ProgramFail(Parser, "invalid operation"); break;
        }

        if (TopValue->Typ->Base == TypeLongLong ||
                TopValue->Typ->Base == TypeUnsignedLongLong ||
                BottomValue->Typ->Base == TypeLongLong ||
                BottomValue->Typ->Base == TypeUnsignedLongLong){
            ExpressionPushUnsignedLongLong(Parser, StackTop, ResultLLInt);
        } else {
            ExpressionPushUnsignedInt(Parser, StackTop, ResultLLInt);
        }
    }
    else if (BottomValue->Typ->Base == TypePointer && IS_NUMERIC_COERCIBLE(TopValue))
    {
        /* pointer/integer infix arithmetic */
        long TopInt = CoerceInteger(TopValue);

        if (Op == TokenEqual || Op == TokenNotEqual)
        {
            /* comparison to a nullptr pointer */
            if (TopInt != 0)
                ProgramFail(Parser, "invalid operation");

            if (Op == TokenEqual)
                ExpressionPushInt(Parser, StackTop, BottomValue->Val->Pointer == nullptr);
            else
                ExpressionPushInt(Parser, StackTop, BottomValue->Val->Pointer != nullptr);
        }
        else if (Op == TokenPlus || Op == TokenMinus)
        {
            /* pointer arithmetic */
            int Size = TypeSize(BottomValue->Typ->FromType, 0, TRUE);

            Pointer = BottomValue->Val->Pointer;
            if (Pointer == nullptr)
                ProgramFail(Parser, "invalid use of a nullptr pointer");

            if (Op == TokenPlus)
                Pointer = (void *)((char *)Pointer + TopInt * Size);
            else
                Pointer = (void *)((char *)Pointer - TopInt * Size);

            StackValue = ExpressionStackPushValueByType(Parser, StackTop, BottomValue->Typ);
            StackValue->Val->Pointer = Pointer;
        }
        else if (Op == TokenAssign && TopInt == 0)
        {
            /* assign a nullptr pointer */
            HeapUnpopStack(Parser->pc, sizeof(Value));
            ExpressionAssign(Parser, BottomValue, TopValue, FALSE, nullptr, 0, FALSE);
            ExpressionStackPushValueNode(Parser, StackTop, BottomValue);
        }
        else if (Op == TokenAddAssign || Op == TokenSubtractAssign)
        {
            /* pointer arithmetic */
            int Size = TypeSize(BottomValue->Typ->FromType, 0, TRUE);

            Pointer = BottomValue->Val->Pointer;
            if (Pointer == nullptr)
                ProgramFail(Parser, "invalid use of a nullptr pointer");

            if (Op == TokenAddAssign)
                Pointer = (void *)((char *)Pointer + TopInt * Size);
            else
                Pointer = (void *)((char *)Pointer - TopInt * Size);

            HeapUnpopStack(Parser->pc, sizeof(Value));
            BottomValue->Val->Pointer = Pointer;
            ExpressionStackPushValueNode(Parser, StackTop, BottomValue);
        }
        else
            ProgramFail(Parser, "invalid operation");
    }
    else if (BottomValue->Typ->Base == TypePointer && TopValue->Typ->Base == TypePointer && Op != TokenAssign)
    {
        /* pointer/pointer operations */
        char *TopLoc = (char *)TopValue->Val->Pointer;
        char *BottomLoc = (char *)BottomValue->Val->Pointer;

        switch (Op)
        {
            case TokenEqual:
                ExpressionPushInt(Parser, StackTop, BottomLoc == TopLoc); break;
            case TokenNotEqual:
                ExpressionPushInt(Parser, StackTop, BottomLoc != TopLoc); break;
            case TokenLessEqual:
                ExpressionPushInt(Parser, StackTop, BottomLoc <= TopLoc); break;
            case TokenLessThan:
                ExpressionPushInt(Parser, StackTop, BottomLoc < TopLoc); break;
            case TokenGreaterEqual:
                ExpressionPushInt(Parser, StackTop, BottomLoc >= TopLoc); break;
            case TokenGreaterThan:
                ExpressionPushInt(Parser, StackTop, BottomLoc > TopLoc); break;
            case TokenMinus:
                ExpressionPushInt(Parser, StackTop, BottomLoc - TopLoc); break;
            default:
                ProgramFail(Parser, "invalid operation"); break;
        }
    }
    else if (Op == TokenAssign)
    {
        /* assign a non-numeric type */
        HeapUnpopStack(Parser->pc, MEM_ALIGN(sizeof(Value)));   /* XXX - possible bug if lvalue is a temp value and takes more than sizeof(Value) */
        ExpressionAssign(Parser, BottomValue, TopValue, FALSE, nullptr, 0, FALSE);
        ExpressionStackPushValueNode(Parser, StackTop, BottomValue);
    }
    else if (Op == TokenCast)
    {
        /* cast a value to a different type */   /* XXX - possible bug if the destination type takes more than sizeof(Value) + sizeof(struct ValueType *) */
        Value *ValueLoc = ExpressionStackPushValueByType(Parser, StackTop, BottomValue->Val->Typ);
        ExpressionAssign(Parser, ValueLoc, TopValue, TRUE, nullptr, 0, TRUE);
        ValueLoc->LValueFrom = TopValue->LValueFrom; // allow things like ((unsigned*)&dmax)[0] = 0x47efffff;
    } else if (Op == TokenOpenBracket){
        // called a function
        ExpressionStackPushValue(Parser, StackTop, TopValue);
    } else if ((BottomValue->Typ->Base == TypeArray && IS_INTEGER_NUMERIC(TopValue))
            || (TopValue->Typ->Base == TypeArray && IS_INTEGER_NUMERIC(BottomValue))
//            || (BottomValue->Typ->Base == TypePointer && IS_INTEGER_NUMERIC(TopValue))
//            || (TopValue->Typ->Base == TypePointer && IS_INTEGER_NUMERIC(BottomValue))
            ) {

        Value *ArrayValue = BottomValue->Typ->Base == TypeArray || BottomValue->Typ->Base == TypePointer ? BottomValue : TopValue;
        long long ArrayIndex = BottomValue->Typ->Base != TypeArray && BottomValue->Typ->Base != TypePointer
                    ? CoerceLongLong(BottomValue) : CoerceLongLong(TopValue);

        int Size = TypeSize(ArrayValue->Typ->FromType, 0, TRUE);

        Pointer = (void*)&(ArrayValue->Val->ArrayMem[0]);
        if (Pointer == nullptr)
            ProgramFail(Parser, "invalid use of a nullptr pointer");

        switch (Op)
        {
            case TokenPlus:
                Pointer = (void *)((char *)Pointer + ArrayIndex * Size); break;
            case TokenMinus:
                Pointer = (void *)((char *)Pointer - ArrayIndex * Size); break;
            default:
                ProgramFail(Parser, "invalid pointer operation");
        }
        StackValue = ExpressionStackPushValueByType(Parser, StackTop,
                                                    TypeGetMatching(Parser->pc, Parser, ArrayValue->Typ->FromType,
                                                                    TypePointer, 0, Parser->pc->StrEmpty,
                                                                    TRUE, nullptr));
        StackValue->Val->Pointer = Pointer;
    } else
        ProgramFail(Parser, "invalid operation");
}

/* take the contents of the expression stack and compute the top until there's nothing greater than the given precedence */
void ExpressionStackCollapse(struct ParseState *Parser, struct ExpressionStack **StackTop, int Precedence, int *IgnorePrecedence)
{
    int FoundPrecedence = Precedence;
    Value *TopValue;
    Value *BottomValue;
    struct ExpressionStack *TopStackNode = *StackTop;
    struct ExpressionStack *TopOperatorNode;

    debugf("ExpressionStackCollapse(%d):\n", Precedence);
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
    while (TopStackNode != nullptr && TopStackNode->Next != nullptr && FoundPrecedence >= Precedence)
    {
        /* find the top operator on the stack */
        if (TopStackNode->Order == OrderNone)
            TopOperatorNode = TopStackNode->Next;
        else
            TopOperatorNode = TopStackNode;

        FoundPrecedence = TopOperatorNode->Precedence;

        /* does it have a high enough precedence? */
        if (FoundPrecedence >= Precedence && TopOperatorNode != nullptr)
        {
            /* execute this operator */
            switch (TopOperatorNode->Order)
            {
                case OrderPrefix:
                    /* prefix evaluation */
                    debugf("prefix evaluation\n");
                    TopValue = TopStackNode->Val;

                    /* pop the value and then the prefix operator - assume they'll still be there until we're done */
                    HeapPopStack(Parser->pc, nullptr, sizeof(struct ExpressionStack) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(TopValue));
                    HeapPopStack(Parser->pc, TopOperatorNode, sizeof(struct ExpressionStack));
                    *StackTop = TopOperatorNode->Next;

                    /* do the prefix operation */
                    if (Parser->Mode == RunModeRun && FoundPrecedence < *IgnorePrecedence )
                    {
                        /* run the operator */
                        ExpressionPrefixOperator(Parser, StackTop, TopOperatorNode->Op, TopValue);
                    }
                    else
                    {
                        /* we're not running it so just return 0 */
                        ExpressionPushInt(Parser, StackTop, 0);
                    }
                    break;

                case OrderPostfix:
                    /* postfix evaluation */
                    debugf("postfix evaluation\n");
                    TopValue = TopStackNode->Next->Val;

                    /* pop the postfix operator and then the value - assume they'll still be there until we're done */
                    HeapPopStack(Parser->pc, nullptr, sizeof(struct ExpressionStack));
                    HeapPopStack(Parser->pc, TopValue, sizeof(struct ExpressionStack) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(TopValue));
                    *StackTop = TopStackNode->Next->Next;

                    /* do the postfix operation */
                    if (Parser->Mode == RunModeRun && FoundPrecedence < *IgnorePrecedence)
                    {
                        /* run the operator */
                        ExpressionPostfixOperator(Parser, StackTop, TopOperatorNode->Op, TopValue);
                    }
                    else
                    {
                        /* we're not running it so just return 0 */
                        ExpressionPushInt(Parser, StackTop, 0);
                    }
                    break;

                case OrderInfix:
                    /* infix evaluation */
                    debugf("infix evaluation\n");
                    TopValue = TopStackNode->Val;
                    if (TopValue != nullptr)
                    {
                        BottomValue = TopOperatorNode->Next->Val;

                        /* pop a value, the operator and another value - assume they'll still be there until we're done */
                        HeapPopStack(Parser->pc, nullptr, sizeof(struct ExpressionStack) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(TopValue));
                        HeapPopStack(Parser->pc, nullptr, sizeof(struct ExpressionStack));
                        HeapPopStack(Parser->pc, BottomValue, sizeof(struct ExpressionStack) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(BottomValue));
                        *StackTop = TopOperatorNode->Next->Next;

                        /* do the infix operation */
                        if (Parser->Mode == RunModeRun && FoundPrecedence <= *IgnorePrecedence)
                        {
                            /* run the operator */
                            ExpressionInfixOperator(Parser, StackTop, TopOperatorNode->Op, BottomValue, TopValue);
                        }
                        else
                        {
                            /* we're not running it so just return 0 */
                            ExpressionPushInt(Parser, StackTop, 0);
                        }
                    }
                    else
                        FoundPrecedence = -1;
                    break;

                case OrderNone:
                    /* this should never happen */
                    assert(TopOperatorNode->Order != OrderNone);
                    break;
            }

            /* if we've returned above the ignored precedence level turn ignoring off */
            if (FoundPrecedence <= *IgnorePrecedence)
                *IgnorePrecedence = DEEP_PRECEDENCE;
        }
#ifdef DEBUG_EXPRESSIONS
        ExpressionStackShow(Parser->pc, *StackTop);
#endif
        TopStackNode = *StackTop;
    }
    debugf("ExpressionStackCollapse() finished\n");
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
}

/* push an operator on to the expression stack */
void ExpressionStackPushOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum OperatorOrder Order, enum LexToken Token, int Precedence)
{
    auto *StackNode = static_cast<ExpressionStack *>(VariableAlloc(Parser->pc, Parser,
                                                                                     sizeof(struct ExpressionStack),
                                                                                     FALSE));
    StackNode->Next = *StackTop;
    StackNode->Order = Order;
    StackNode->Op = Token;
    StackNode->Precedence = Precedence;
    *StackTop = StackNode;
    debugf("ExpressionStackPushOperator()\n");
#ifdef FANCY_ERROR_MESSAGES
    StackNode->Line = Parser->Line;
    StackNode->CharacterPos = Parser->CharacterPos;
#endif
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
}

/* do the '.' and '->' operators */
void ExpressionGetStructElement(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Token)
{
    Value *Ident;

    /* get the identifier following the '.' or '->' */
    if (LexGetToken(Parser, &Ident, TRUE) != TokenIdentifier)
        ProgramFail(Parser, "need an structure or union member after '%s'", (Token == TokenDot) ? "." : "->");

    if (Parser->Mode == RunModeRun)
    {
        /* look up the struct element */
        Value *ParamVal = (*StackTop)->Val;
        Value *StructVal = ParamVal;
        struct ValueType *StructType = ParamVal->Typ;
        char *DerefDataLoc = (char *)ParamVal->Val;
        Value *MemberValue = nullptr;
        Value *Result;

        /* if we're doing '->' dereference the struct pointer first */
        if (Token == TokenArrow)
            DerefDataLoc = static_cast<char *>(VariableDereferencePointer(Parser, ParamVal, &StructVal, nullptr,
                                                                          &StructType, nullptr));

        if (DerefDataLoc == nullptr)
            ProgramFail(Parser, "the struct hasn't been initialized yet");

        if (StructType->Base != TypeStruct && StructType->Base != TypeUnion)
            ProgramFail(Parser, "can't use '%s' on something that's not a struct or union %s : it's a %t", (Token == TokenDot) ? "." : "->", (Token == TokenArrow) ? "pointer" : "", ParamVal->Typ);

        if (!TableGet(StructType->Members, Ident->Val->Identifier, &MemberValue, nullptr, nullptr, nullptr))
            ProgramFail(Parser, "doesn't have a member called '%s'", Ident->Val->Identifier);

        /* pop the value - assume it'll still be there until we're done */
        HeapPopStack(Parser->pc, ParamVal, sizeof(struct ExpressionStack) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(StructVal));
        *StackTop = (*StackTop)->Next;

        /* make the result value for this member only */
        Result = VariableAllocValueFromExistingData(Parser, MemberValue->Typ,
                                                    (AnyValue *) (DerefDataLoc + MemberValue->Val->Integer), TRUE,
                                                    (StructVal != nullptr) ? StructVal->LValueFrom : nullptr, nullptr);
        Result->BitField = MemberValue->BitField;                                                                        
        Result->ConstQualifier = MemberValue->ConstQualifier;
        ExpressionStackPushValueNode(Parser, StackTop, Result);
    }
}

char * ExpressionResolveFunctionName(struct ParseState * Parser, Value * LexValue) {
    Value * DefinedVal;

    VariableGet(Parser->pc, Parser, LexValue->Val->Identifier, &DefinedVal);

    if (DefinedVal->Typ == &Parser->pc->FunctionPtrType){
        if (DefinedVal->Val->Identifier == nullptr)
            ProgramFail(Parser, "trying to call nullptr function pointer");
        return DefinedVal->Val->Identifier;
    } else {
        return LexValue->Val->Identifier;
    }
}

/* parse an expression with operator precedence */
int ExpressionParse(struct ParseState *Parser, Value **Result)
{
    Value *LexValue;
    int PrefixState = TRUE;
    int Done = FALSE;
    int BracketPrecedence = 0;
    int LocalPrecedence;
    int Precedence = 0;
    int IgnorePrecedence = DEEP_PRECEDENCE;
    struct ExpressionStack *StackTop = nullptr;
    int TernaryDepth = 0;

    debugf("ExpressionParse():\n");

    // save the ret function
    const char *RetBeforeName = Parser->ReturnFromFunction;
    Parser->ReturnFromFunction = nullptr;

    do
    {
        struct ParseState PreState;
        enum LexToken Token;

        ParserCopy(&PreState, Parser);
        Token = LexGetToken(Parser, &LexValue, TRUE);

        if ((( (int)Token > TokenComma && (int)Token <= (int)TokenOpenBracket) ||
               (Token == TokenCloseBracket && BracketPrecedence != 0)) &&
               (Token != TokenColon || TernaryDepth > 0) )
        {
            /* it's an operator with precedence */
            if (PrefixState)
            {
                /* expect a prefix operator */
                if (OperatorPrecedence[(int)Token].PrefixPrecedence == 0)
                    ProgramFail(Parser, "operator not expected here");

                LocalPrecedence = OperatorPrecedence[(int)Token].PrefixPrecedence;
                Precedence = BracketPrecedence + LocalPrecedence;

                if (Token == TokenOpenBracket)
                {
                    /* it's either a new bracket level or a cast */
                    enum LexToken BracketToken = LexGetToken(Parser, &LexValue, FALSE);
                    if (IsTypeToken(Parser, BracketToken, LexValue) && (StackTop == nullptr || StackTop->Op != TokenSizeof) )
                    {
                        /* it's a cast - get the new type */
                        struct ValueType *CastType;
                        char *CastIdentifier;
                        Value *CastTypeValue;

                        int IsConst;
                        TypeParse(Parser, &CastType, &CastIdentifier, nullptr, &IsConst, 0);
                        if (LexGetToken(Parser, &LexValue, TRUE) != TokenCloseBracket)
                            ProgramFail(Parser, "brackets not closed");

                        /* scan and collapse the stack to the precedence of this infix cast operator, then push */
                        Precedence = BracketPrecedence + OperatorPrecedence[(int)TokenCast].PrefixPrecedence;

                        ExpressionStackCollapse(Parser, &StackTop, Precedence+1, &IgnorePrecedence);
                        CastTypeValue = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->TypeType, FALSE, nullptr, FALSE);
                        CastTypeValue->Val->Typ = CastType;
                        CastTypeValue->ConstQualifier = IsConst;
                        ExpressionStackPushValueNode(Parser, &StackTop, CastTypeValue);
                        ExpressionStackPushOperator(Parser, &StackTop, OrderInfix, TokenCast, Precedence);
                    }
                    else
                    {
                        /* boost the bracket operator precedence */
                        BracketPrecedence += BRACKET_PRECEDENCE;
                    }
                }
                else
                {
                    /* scan and collapse the stack to the precedence of this operator, then push */

                    /* take some extra care for double prefix operators, e.g. x = - -5, or x = **y */
                    int NextToken = LexGetToken(Parser, nullptr, FALSE);
                    int TempPrecedenceBoost = 0;
                    if (NextToken > TokenComma && NextToken < TokenOpenBracket)
                    {
                        int NextPrecedence = OperatorPrecedence[(int)NextToken].PrefixPrecedence;

                        /* two prefix operators with equal precedence? make sure the innermost one runs first */
                        /* FIXME - probably not correct, but can't find a test that fails at this */
                        if (LocalPrecedence == NextPrecedence)
                            TempPrecedenceBoost = -1;
                    }

                    ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                    ExpressionStackPushOperator(Parser, &StackTop, OrderPrefix, Token, Precedence + TempPrecedenceBoost);
                }
            }
            else
            {
                /* expect an infix or postfix operator */
                if (OperatorPrecedence[(int)Token].PostfixPrecedence != 0)
                {
                    Value * TopValue;
                    switch (Token)
                    {
                        case TokenCloseBracket:
                        case TokenRightSquareBracket:
                            if (BracketPrecedence == 0)
                            {
                                /* assume this bracket is after the end of the expression */
                                ParserCopy(Parser, &PreState);
                                Done = TRUE;
                            }
                            else
                            {
                                /* collapse to the bracket precedence */
                                ExpressionStackCollapse(Parser, &StackTop, BracketPrecedence, &IgnorePrecedence);
                                BracketPrecedence -= BRACKET_PRECEDENCE;
                            }
                            break;
                        case TokenQuestionMark:
                            Precedence = BracketPrecedence + OperatorPrecedence[(int)Token].PostfixPrecedence;

                            ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                            ExpressionStackPushOperator(Parser, &StackTop, OrderPostfix, Token, Precedence);
                            ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                            PrefixState = TRUE;
                            TernaryDepth++;
                            TopValue = StackTop->Val;
                            HeapPopStack(Parser->pc, TopValue, sizeof(struct ExpressionStack) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(TopValue));
                            StackTop = StackTop->Next;
                            if (!CoerceInteger(TopValue)){
                                LexToken NextToken = LexGetToken(Parser, nullptr, TRUE);
                                int depth = 1;
                                while (depth > 0 && NextToken != TokenEOF){
                                    if (NextToken == TokenQuestionMark) ++depth;
                                    else if (NextToken == TokenColon) --depth;
                                    if (depth > 0)
                                        NextToken = LexGetToken(Parser, nullptr, TRUE);
                                }
                            }
                            break;
                        default:
                            /* scan and collapse the stack to the precedence of this operator, then push */
                            Precedence = BracketPrecedence + OperatorPrecedence[(int)Token].PostfixPrecedence;
                            ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                            ExpressionStackPushOperator(Parser, &StackTop, OrderPostfix, Token, Precedence);
                            break;
                    }
                }
                else if (OperatorPrecedence[(int)Token].InfixPrecedence != 0)
                {
                    /* scan and collapse the stack, then push */
                    Precedence = BracketPrecedence + OperatorPrecedence[(int)Token].InfixPrecedence;

                    /* for right to left order, only go down to the next higher precedence so we evaluate it in reverse order */
                    /* for left to right order, collapse down to this precedence so we evaluate it in forward order */
                    if (IS_LEFT_TO_RIGHT(OperatorPrecedence[(int)Token].InfixPrecedence))
                        ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                    else
                        ExpressionStackCollapse(Parser, &StackTop, Precedence+1, &IgnorePrecedence);

                    if (Token == TokenDot || Token == TokenArrow)
                    {
                        ExpressionGetStructElement(Parser, &StackTop, Token); /* this operator is followed by a struct element so handle it as a special case */
                    }
                    else
                    {
                        /* if it's a && or || operator we may not need to evaluate the right hand side of the expression */
                        if ( (Token == TokenLogicalOr || Token == TokenLogicalAnd) && IS_NUMERIC_COERCIBLE(StackTop->Val))
                        {
                            long LHSInt = CoerceLongLong(StackTop->Val);
                            if ( ( (Token == TokenLogicalOr && LHSInt) || (Token == TokenLogicalAnd && !LHSInt))
                                 && (IgnorePrecedence > Precedence) )
                                IgnorePrecedence = Precedence;
                        }

                        /* push the operator on the stack */
                        ExpressionStackPushOperator(Parser, &StackTop, OrderInfix, Token, Precedence);
                        PrefixState = TRUE;

                        if (Token == TokenColon){
                            TernaryDepth--;
                            if (IgnorePrecedence <= Precedence)
                                IgnorePrecedence = DEEP_PRECEDENCE; // enable execution again
                            else
                                IgnorePrecedence = Precedence;
                        }
                    }

                    /* treat an open square bracket as an infix array index operator followed by an open bracket */
                    if (Token == TokenLeftSquareBracket)
                    {
                        /* boost the bracket operator precedence, then push */
                        BracketPrecedence += BRACKET_PRECEDENCE;
                    }

                    if (Token == TokenOpenBracket) {
                        char RunIt = Parser->Mode == RunModeRun && Precedence < IgnorePrecedence;
                        if (RunIt)
                            Parser->EnterFunction = StackTop->Next->Val->Val->Identifier;
                        ExpressionParseFunctionCall(Parser, &StackTop,
                            RunIt ? StackTop->Next->Val->Val->Identifier : Parser->pc->StrEmpty, RunIt);
                    }
                }
                else
                    ProgramFail(Parser, "operator not expected here");
            }
        }
        else if (Token == TokenIdentifier)
        {
            /* it's a variable, function or a macro */
            if (!PrefixState)
                ProgramFail(Parser, "identifier not expected here");

            if (LexGetToken(Parser, nullptr, FALSE) == TokenOpenBracket)
            {
                char RunIt = Parser->Mode == RunModeRun && Precedence < IgnorePrecedence;
                char *FuncName = RunIt ? ExpressionResolveFunctionName(Parser, LexValue) : LexValue->Val->Identifier;
                Value * FuncValue;
                if (RunIt){
                    VariableGet(Parser->pc, Parser, FuncName, &FuncValue);
                    if (FuncValue->Val->FuncDef.Intrinsic != nullptr
                            && FuncName != TableStrRegister(Parser->pc, "__VERIFIER_assume")){
                        Parser->SkipIntrinsic = TRUE;
                    }
                }

                if (Parser->DebugMode && RunIt) {
                    Parser->EnterFunction = FuncName;
                    DebugCheckStatement(Parser);
                    Parser->EnterFunction = nullptr;
                }
                
                LexGetToken(Parser, nullptr, TRUE);
                ExpressionParseFunctionCall(Parser, &StackTop, FuncName, RunIt);
                if (Parser->DebugMode && Parser->Mode == RunModeRun) {
                    DebugCheckStatement(Parser);
                    Parser->ReturnFromFunction = nullptr;
                }
                
                if (Parser->SkipIntrinsic == TRUE)
                    Parser->SkipIntrinsic = FALSE;
            }
            else
            {
                if (Parser->Mode == RunModeRun /* && Precedence < IgnorePrecedence */)
                {
                    Value *VariableValue = nullptr;

                    VariableGet(Parser->pc, Parser, LexValue->Val->Identifier, &VariableValue);
                    if (VariableValue->Typ->Base == TypeMacro)
                    {
                        /* evaluate a macro as a kind of simple subroutine */
                        struct ParseState MacroParser;
                        Value *MacroResult;

                        ParserCopy(&MacroParser, &VariableValue->Val->MacroDef.Body);
                        MacroParser.Mode = Parser->Mode;
                        if (VariableValue->Val->MacroDef.NumParams != 0)
                            ProgramFail(&MacroParser, "macro arguments missing");

                        if (!ExpressionParse(&MacroParser, &MacroResult) || LexGetToken(&MacroParser, nullptr, FALSE) != TokenEndOfFunction)
                            ProgramFail(&MacroParser, "expression expected");

                        ExpressionStackPushValueNode(Parser, &StackTop, MacroResult);
                    }
                    else if (VariableValue->Typ == &Parser->pc->VoidType)
                        ProgramFail(Parser, "a void value isn't much use here");
                    else if (VariableValue->Typ == &Parser->pc->FunctionType){ // it's a func ptr identifier
                        Value * FPtr = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->FunctionPtrType, TRUE, FALSE, TRUE);
                        FPtr->Val->Identifier = LexValue->Val->Identifier;
                        ExpressionStackPushValue(Parser, &StackTop, FPtr);
                        VariableFree(Parser->pc, FPtr);
                    }
                    else
                        ExpressionStackPushLValue(Parser, &StackTop, VariableValue, 0); /* it's a value variable */
                }
                else /* push a dummy value */
                    ExpressionPushInt(Parser, &StackTop, 0);

            }

             /* if we've successfully ignored the RHS turn ignoring off */
            if (Precedence <= IgnorePrecedence)
                IgnorePrecedence = DEEP_PRECEDENCE;

            PrefixState = FALSE;
        }
        else if ((int)Token > TokenCloseBracket && (int)Token <= TokenCharacterConstant)
        {
            /* it's a value of some sort, push it */
            if (!PrefixState)
                ProgramFail(Parser, "value not expected here");

            PrefixState = FALSE;
            if (IgnorePrecedence <= Precedence)
                ExpressionStackPushValueByType(Parser, &StackTop, &Parser->pc->VoidType);
            else
                ExpressionStackPushValue(Parser, &StackTop, LexValue);
        }
        else if (IsTypeToken(Parser, Token, LexValue))
        {
            /* it's a type. push it on the stack like a value. this is used in sizeof() */
            struct ValueType *Typ;
            char *Identifier;
            Value *TypeValue;

            if (!PrefixState)
                ProgramFail(Parser, "type not expected here");

            PrefixState = FALSE;
            ParserCopy(Parser, &PreState);
            TypeParse(Parser, &Typ, &Identifier, nullptr, nullptr, 0);
            TypeValue = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->TypeType, FALSE, nullptr, FALSE);
            TypeValue->Val->Typ = Typ;
            ExpressionStackPushValueNode(Parser, &StackTop, TypeValue);
        }
        else
        {
            /* it isn't a token from an expression */
            ParserCopy(Parser, &PreState);
            Done = TRUE;
        }

    } while (!Done);

    /* check that brackets have been closed */
    if (BracketPrecedence > 0)
        ProgramFail(Parser, "brackets not closed");

    /* scan and collapse the stack to precedence 0 */
    ExpressionStackCollapse(Parser, &StackTop, 0, &IgnorePrecedence);

    /* fix up the stack and return the result if we're in run mode */
    if (StackTop != nullptr)
    {
        /* all that should be left is a single value on the stack */
        if (Parser->Mode == RunModeRun)
        {
            if (StackTop->Order != OrderNone || StackTop->Next != nullptr)
                ProgramFail(Parser, "invalid expression");

            *Result = StackTop->Val;
            HeapPopStack(Parser->pc, StackTop, sizeof(struct ExpressionStack));
        }
        else
            HeapPopStack(Parser->pc, StackTop->Val, sizeof(struct ExpressionStack) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(StackTop->Val));
    }

    Parser->ReturnFromFunction = RetBeforeName;
    /* After parsing an expression, now all of the assignments would be finished -> important for assumptions. */
    if (Parser->DebugMode && Parser->Mode == RunModeRun) {
        DebugCheckStatement(Parser);
    }
    Parser->ReturnFromFunction = nullptr;
    Parser->LastConditionBranch = ConditionUndefined;

    debugf("ExpressionParse() done\n\n");
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, StackTop);
#endif
    return StackTop != nullptr;
}


/* do a parameterised macro call */
void ExpressionParseMacroCall(struct ParseState *Parser, struct ExpressionStack **StackTop, const char *MacroName, struct MacroDef *MDef)
{
    Value *ReturnValue = nullptr;
    Value *Param;
    Value **ParamArray = nullptr;
    int ArgCount;
    enum LexToken Token;

    if (Parser->Mode == RunModeRun)
    {
        /* create a stack frame for this macro */
#ifndef NO_FP
        ExpressionStackPushValueByType(Parser, StackTop, &Parser->pc->DoubleType);  /* largest return type there is */
#else
        ExpressionStackPushValueByType(Parser, StackTop, &Parser->pc->IntType);  /* largest return type there is */
#endif
        ReturnValue = (*StackTop)->Val;
        HeapPushStackFrame(Parser->pc);
        ParamArray = static_cast<Value **>(HeapAllocStack(Parser->pc, sizeof(Value *) * MDef->NumParams));
        if (ParamArray == nullptr)
            ProgramFail(Parser, "out of memory");
    }
    else
        ExpressionPushLongLong(Parser, StackTop, 0);

    /* parse arguments */
    ArgCount = 0;
    do {
        if (ExpressionParse(Parser, &Param))
        {
            if (Parser->Mode == RunModeRun)
            {
                if (ArgCount < MDef->NumParams)
                    ParamArray[ArgCount] = Param;
                else
                    ProgramFail(Parser, "too many arguments to %s()", MacroName);
            }

            ArgCount++;
            Token = LexGetToken(Parser, nullptr, TRUE);
            if (Token != TokenComma && Token != TokenCloseBracket)
                ProgramFail(Parser, "comma expected");
        }
        else
        {
            /* end of argument list? */
            Token = LexGetToken(Parser, nullptr, TRUE);
            if (Token!=TokenCloseBracket)
                ProgramFail(Parser, "bad argument");
        }

    } while (Token != TokenCloseBracket);

    if (Parser->Mode == RunModeRun)
    {
        /* evaluate the macro */
        struct ParseState MacroParser{};
        int Count;
        Value *EvalValue;

        if (ArgCount < MDef->NumParams)
            ProgramFail(Parser, "not enough arguments to '%s'", MacroName);

        if (MDef->Body.Pos == nullptr)
            ProgramFail(Parser, "'%s' is undefined", MacroName);

        ParserCopy(&MacroParser, &MDef->Body);
        MacroParser.Mode = Parser->Mode;
        VariableStackFrameAdd(Parser, MacroName, 0);
        Parser->pc->TopStackFrame->NumParams = ArgCount;
        Parser->pc->TopStackFrame->ReturnValue = ReturnValue;
        for (Count = 0; Count < MDef->NumParams; Count++)
            VariableDefine(Parser->pc, Parser, MDef->ParamName[Count], ParamArray[Count], nullptr, TRUE, false);

        ExpressionParse(&MacroParser, &EvalValue);
        ExpressionAssign(Parser, ReturnValue, EvalValue, TRUE, MacroName, 0, FALSE);
        VariableStackFramePop(Parser);
        HeapPopStackFrame(Parser->pc);
    }
}

/* do a function call */
void ExpressionParseFunctionCall(struct ParseState *Parser, struct ExpressionStack **StackTop, const char *FuncName, int RunIt)
{
    Value *ReturnValue = nullptr;
    Value *FuncValue = nullptr;
    Value *Param;
    Value **ParamArray = nullptr;
    int ArgCount;
    enum LexToken Token;    /* open bracket */
    enum RunMode OldMode = Parser->Mode;

    if (RunIt)
    {
        /* get the function definition */
        VariableGet(Parser->pc, Parser, FuncName, &FuncValue);

        if (FuncValue->Typ->Base == TypeMacro)
        {
            /* this is actually a macro, not a function */
            ExpressionParseMacroCall(Parser, StackTop, FuncName, &FuncValue->Val->MacroDef);
            return;
        }

        if (FuncValue->Typ->Base != TypeFunction)
            ProgramFail(Parser, "%t is not a function - can't call", FuncValue->Typ);

        ExpressionStackPushValueByType(Parser, StackTop, FuncValue->Val->FuncDef.ReturnType);
        ReturnValue = (*StackTop)->Val;
        HeapPushStackFrame(Parser->pc);
        ParamArray = static_cast<Value **>(HeapAllocStack(Parser->pc,
                                                          sizeof(Value *) * FuncValue->Val->FuncDef.NumParams));
        if (ParamArray == nullptr)
            ProgramFail(Parser, "out of memory");
    }
    else
    {
        ExpressionPushInt(Parser, StackTop, 0);
//        Parser->Mode = RunModeSkip;
        Parser->Mode = Parser->Mode == RunModeGoto ? RunModeGoto : RunModeSkip;
    }

    /* parse arguments */
    ArgCount = 0;
    do {
        if (RunIt && ArgCount < FuncValue->Val->FuncDef.NumParams)
            ParamArray[ArgCount] = VariableAllocValueFromType(Parser->pc, Parser, FuncValue->Val->FuncDef.ParamType[ArgCount], FALSE, nullptr, FALSE);

        if (ExpressionParse(Parser, &Param))
        {
            if (RunIt)
            {
                if (ArgCount < FuncValue->Val->FuncDef.NumParams)
                {
                    ExpressionAssign(Parser, ParamArray[ArgCount], Param, TRUE, FuncName, ArgCount+1, FALSE);
                    VariableStackPop(Parser, Param);
                }
                else
                {
                    if (!FuncValue->Val->FuncDef.VarArgs)
                        ProgramFail(Parser, "too many arguments to %s()", FuncName);
                }
            }

            ArgCount++;
            Token = LexGetToken(Parser, nullptr, TRUE);
            if (Token != TokenComma && Token != TokenCloseBracket)
                ProgramFail(Parser, "comma expected");
        }
        else
        {
            /* end of argument list? */
            Token = LexGetToken(Parser, nullptr, TRUE);
            if (Token!=TokenCloseBracket)
                ProgramFail(Parser, "bad argument");
        }

    } while (Token != TokenCloseBracket);

    if (RunIt)
    {
        Parser->EnterFunction = nullptr; // remove the enter function identifier before call
        /* run the function */
        if (ArgCount < FuncValue->Val->FuncDef.NumParams)
            ProgramFail(Parser, "not enough arguments to '%s'", FuncName);

        if (FuncValue->Val->FuncDef.Intrinsic == nullptr)
        {
            /* run a user-defined function */
            struct ParseState FuncParser{};
            int Count;
            int OldScopeID = Parser->ScopeID;

            if (FuncValue->Val->FuncDef.Body.Pos == nullptr)
                ProgramFail(Parser, "'%s' is undefined", FuncName);

            ParserCopy(&FuncParser, &FuncValue->Val->FuncDef.Body);
            VariableStackFrameAdd(Parser, FuncName, FuncValue->Val->FuncDef.Intrinsic ? FuncValue->Val->FuncDef.NumParams : 0);
            Parser->pc->TopStackFrame->NumParams = ArgCount;
            Parser->pc->TopStackFrame->ReturnValue = ReturnValue;

            /* Function parameters should not go out of scope */
            Parser->ScopeID = -1;

            for (Count = 0; Count < FuncValue->Val->FuncDef.NumParams; Count++) {
                Value * defined = VariableDefine(Parser->pc, Parser, FuncValue->Val->FuncDef.ParamName[Count],
                        ParamArray[Count], nullptr, TRUE, false);
                if (ParamArray[Count]->Typ->Base == TypeArray){
                    defined->Val = ParamArray[Count]->Val;
                }
            }
            Parser->ScopeID = OldScopeID;

            // track the function
            const char * FunctionBefore = Parser->CurrentFunction;
            Parser->CurrentFunction = FuncName;

            if (ParseStatement(&FuncParser, TRUE) != ParseResultOk)
                ProgramFail(&FuncParser, "function body expected");

            if (RunIt)
            {
                if (FuncParser.Mode == RunModeRun && FuncValue->Val->FuncDef.ReturnType != &Parser->pc->VoidType && strcmp("main", FuncName) != 0)
                    fprintf(stderr, "no value returned from a function returning %t", FuncValue->Val->FuncDef.ReturnType);

                else if (FuncParser.Mode == RunModeGoto){
                    do {
                        ParserCopyPos(&FuncParser, &FuncValue->Val->FuncDef.Body);
                        FuncParser.FreshGotoSearch = FALSE;
                        ParseStatement(&FuncParser, TRUE);
                    } while (FuncParser.Mode == RunModeGoto && FuncParser.FreshGotoSearch == TRUE);
                    if (FuncParser.Mode == RunModeGoto)
                        ProgramFail(&FuncParser, "couldn't find goto label '%s'", FuncParser.SearchGotoLabel);
                }
            }
            Parser->CurrentFunction = FunctionBefore;

            VariableStackFramePop(Parser);
        }
        else {
            FuncValue->Val->FuncDef.Intrinsic(Parser, ReturnValue, ParamArray, ArgCount);
        }
        Parser->ReturnFromFunction = FuncName;

        HeapPopStackFrame(Parser->pc);
    }
    Parser->Mode = OldMode == RunModeGoto ? Parser->Mode : OldMode;
}

/* parse an expression */
long long ExpressionParseLongLong(struct ParseState *Parser)
{
    Value *Val = nullptr;
    long long Result = 0;

    if (!ExpressionParse(Parser, &Val))
        ProgramFail(Parser, "expression expected");

    if (Parser->Mode == RunModeRun)
    {
        if (!IS_NUMERIC_COERCIBLE_PLUS_POINTERS(Val, true))
            ProgramFail(Parser, "integer value expected instead of %t", Val->Typ);

        Result = CoerceLongLong(Val);
        VariableStackPop(Parser, Val);
    }

    return Result;
}

