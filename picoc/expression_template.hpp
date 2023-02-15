#include "interpreter.hpp"

namespace nitwit {
#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS
#define EXPR_TEMPLATE_PREFIX nitwit::assumptions::
#define EXPR_TEMPLATE_STRING_PREFIX "Assumptions - "
    namespace assumptions {
#else
#define EXPR_TEMPLATE_PREFIX nitwit::expressions::
#define EXPR_TEMPLATE_STRING_PREFIX "Expressions - "
    namespace expressions {
#endif

/* picoc expression evaluator - a stack-based expression evaluation system
 * which handles operator precedence */

/* whether evaluation is left to right for a given precedence level */
#define IS_LEFT_TO_RIGHT(p) ((p) != 2 && (p) != 14)
#define BRACKET_PRECEDENCE 20

#define DEEP_PRECEDENCE (BRACKET_PRECEDENCE*1000)

#ifdef DEBUG_EXPRESSIONS
#define debugf printf
#else
#define debugf(...)
#endif

#ifdef DEBUG_EXPRESSIONS_SHOW_ADDRESSES
#define hideAddress(addr) (addr)
#else
#define hideAddress(addr) 0ull
#endif

void ResolvedVariable(struct ParseState* Parser, const char* Identifier, Value* VariableValue);
#include "ResolveVariable.hpp"

/* local prototypes */
enum class OperatorOrder
{
    OrderNone,
    OrderPrefix,
    OrderInfix,
    OrderPostfix
};

/* a stack of expressions we use in evaluation */
struct ExpressionStack
{
    ExpressionStack *Next;       /* the next lower item on the stack */
    Value *Val;                  /* the value for this stack node */
    LexToken Op;                   /* the operator */
    short unsigned int Precedence;      /* the operator precedence of this node */
    OperatorOrder Order;                /* the evaluation order of this operator */
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
void ExpressionStackShow(Picoc* pc, struct ExpressionStack* StackTop) {
    printf(EXPR_TEMPLATE_STRING_PREFIX "Expression stack [0x%llx,0x%llx]: ", hideAddress((unsigned long long)pc->HeapStackTop), hideAddress((unsigned long long)StackTop));

    while (StackTop != nullptr)
    {
        if (StackTop->Order == OperatorOrder::OrderNone)
        {
            /* it's a value */
            if (StackTop->Val->IsLValue)
                printf("lvalue=");
            else
                printf("value=");

            switch (StackTop->Val->Typ->Base)
            {
            case BaseType::TypeVoid:      printf("void"); break;
            case BaseType::TypeInt:       printf("%d:int", StackTop->Val->Val->Integer); break;
            case BaseType::TypeShort:     printf("%d:short", StackTop->Val->Val->ShortInteger); break;
            case BaseType::TypeChar:      printf("%d:char", StackTop->Val->Val->Character); break;
            case BaseType::TypeLong:      printf("%ld:long", StackTop->Val->Val->LongInteger); break;
            case BaseType::TypeLongLong:  printf("%lld:long long", StackTop->Val->Val->LongLongInteger); break;
            case BaseType::TypeUnsignedShort: printf("%d:unsigned short", StackTop->Val->Val->UnsignedShortInteger); break;
            case BaseType::TypeUnsignedInt: printf("%d:unsigned int", StackTop->Val->Val->UnsignedInteger); break;
            case BaseType::TypeUnsignedLong: printf("%ld:unsigned long", StackTop->Val->Val->UnsignedLongInteger); break;
            case BaseType::TypeUnsignedLongLong: printf("%llud:unsigned long long", StackTop->Val->Val->UnsignedLongLongInteger); break;
            case BaseType::TypeDouble:        printf("%f:fp", StackTop->Val->Val->Double); break;
            case BaseType::TypeFunction:  printf("%s:function", StackTop->Val->Val->Identifier); break;
            case BaseType::TypeMacro:     printf("%s:macro", StackTop->Val->Val->Identifier); break;
            case BaseType::TypePointer:
                if (StackTop->Val->Val->Pointer == nullptr)
                    printf("ptr(nullptr)");
                else if (StackTop->Val->Typ->FromType->Base == BaseType::TypeChar)
                    printf("\"%s\":string", (char*)StackTop->Val->Val->Pointer);
                else
                    printf("ptr(0x%llx)", (unsigned long long)StackTop->Val->Val->Pointer);
                break;
            case BaseType::TypeArray:     printf("array"); break;
            case BaseType::TypeStruct:    printf("struct"); break;
            case BaseType::TypeUnion:     printf("union"); break;
            case BaseType::TypeEnum:      printf("enum"); break;
            case BaseType::Type_Type:     PrintType(StackTop->Val->Val->Typ, pc->CStdOut); printf(":type"); break;
            default:            printf("unknown"); break;
            }
            printf("[0x%llx,0x%llx]", hideAddress((unsigned long long)StackTop), hideAddress((unsigned long long)StackTop->Val));
        }
        else
        {
            /* it's an operator */
            printf("op='%s' %s %d", OperatorPrecedence[(int)StackTop->Op].Name,
                (StackTop->Order == OperatorOrder::OrderPrefix) ? "prefix" : ((StackTop->Order == OperatorOrder::OrderPostfix) ? "postfix" : "infix"),
                StackTop->Precedence);
            printf("[0x%llx]", hideAddress((unsigned long long)StackTop));
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
        if (VariableDefined(Parser->pc, (const char*) LexValue->Val->Pointer))
        {
            VariableGet(Parser->pc, Parser, (const char*) LexValue->Val->Pointer, &VarValue);
            if (VarValue->Typ->Base == BaseType::Type_Type)
                return 1;
        }
    }

    return 0;
}

/* push a node on to the expression stack */
void ExpressionStackPushValueNode(struct ParseState* Parser, struct ExpressionStack** StackTop, Value* ValueLoc)
{
    auto* StackNode = static_cast<ExpressionStack*>(VariableAlloc(Parser->pc, Parser, MEM_ALIGN(sizeof(struct ExpressionStack)), FALSE));
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

template<typename T>
void ExpressionPushT(ParseState* Parser, ExpressionStack** StackTop, T const& value, bool isNd) {
    Value* ValueLoc = VariableAllocValueFromType(Parser->pc, Parser, typeByT<T>(Parser, isNd), FALSE, nullptr, FALSE);
    AssignT_Pure<T>(Parser, ValueLoc, value);
    ExpressionStackPushValueNode(Parser, StackTop, ValueLoc);
}

template<typename T>
void PrefixOperation(ParseState* Parser, Value* TopValue, ExpressionStack** StackTop, LexToken const& Op) {
    using OpResultType_t = typename std::conditional<(std::is_same_v<T, char> || std::is_same_v<T, unsigned char> || std::is_same_v<T, short> || std::is_same_v<T, unsigned short>), int, T>::type;
    /* integer/fp prefix arithmetic */
    T Result = 0;
    OpResultType_t ResultOfOp = 0;
    int ResultNot = 0;
    T const TopInt = CoerceT<T>(TopValue);
    switch (Op)
    {
    case TokenPlus:
        ResultOfOp = TopInt;
        ExpressionPushT<OpResultType_t>(Parser, StackTop, ResultOfOp, TypeIsNonDeterministic(TopValue->Typ));
        break;
    case TokenMinus:
        ResultOfOp = -TopInt;
        ExpressionPushT<OpResultType_t>(Parser, StackTop, ResultOfOp, TypeIsNonDeterministic(TopValue->Typ));
        break;
    case TokenIncrement:
        Result = AssignT<T>(Parser, TopValue, TopInt + 1, false);
        ExpressionPushT<T>(Parser, StackTop, Result, TypeIsNonDeterministic(TopValue->Typ));
        break;
    case TokenDecrement:
        Result = AssignT<T>(Parser, TopValue, TopInt - 1, false);
        ExpressionPushT<T>(Parser, StackTop, Result, TypeIsNonDeterministic(TopValue->Typ));
        break;
    case TokenUnaryNot:
        ResultNot = !TopInt;
        ExpressionPushT<int>(Parser, StackTop, ResultNot, TypeIsNonDeterministic(TopValue->Typ));
        break;
    case TokenUnaryExor:
        if constexpr (!(std::is_same_v<T, float> || std::is_same_v<T, double>)) {
            ResultOfOp = ~TopInt;
            ExpressionPushT<OpResultType_t>(Parser, StackTop, ResultOfOp, TypeIsNonDeterministic(TopValue->Typ));
            break;
        }
        [[fallthrough]];
    default:
        ProgramFail(Parser, "Invalid Operation '%s' on type %s.", tokenToString(Op), getType(TopValue));
        break;
    }
}

template <typename T>
void PostfixOperation(ParseState* Parser, Value* TopValue, ExpressionStack** StackTop, LexToken const& Op) {
    T ResultValue = 0;
    T const TopInt = CoerceT<T>(TopValue);
    T const one = 1;
    switch (Op)
    {
    case TokenIncrement:
        ResultValue = AssignT<T>(Parser, TopValue, TopInt + one, true);
        break;
    case TokenDecrement:
        ResultValue = AssignT<T>(Parser, TopValue, TopInt - one, true);
        break;
    case TokenRightSquareBracket:   ProgramFail(Parser, "not supported"); break;  /* XXX */
    case TokenCloseBracket:         ProgramFail(Parser, "not supported"); break;  /* XXX */
    default:
        ProgramFail(Parser, "Invalid Operation '%s' on type %s.", tokenToString(Op), getType(TopValue));
        break;
    }

    ExpressionPushT<T>(Parser, StackTop, ResultValue, TypeIsNonDeterministic(TopValue->Typ));
}

enum class OpType {
    INVALID, ASSIGNMENT, COMPARISON, STANDARD, SHIFT
};

template<typename B, typename T>
int handleEqualityCheck(B const& b, T const& t) {
    if constexpr ((std::is_same_v<B, float> || std::is_same_v<B, double>) && (std::is_same_v<T, float> || std::is_same_v<T, double>)) {
        if (b == t) {
            return true;
        }
#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS
        // NaN should only be equal to itself in the case of Assumptions, where we want to validate values
        if (std::isnan(b) && std::isnan(t)) {
            return true;
        }
#endif
        return false;
    }
    else {
        return b == t;
    }
}

template <typename B, typename T>
void InfixOperation(ParseState* Parser, Value* BottomValue, Value* TopValue, ExpressionStack** StackTop, LexToken const& Op) {
    /* infix arithmetic */

    using ResultType_t = typename 
        std::conditional<(std::is_same_v<B, double> || std::is_same_v<T, double>), double, // if one operand is double, the result is double
        typename std::conditional<(std::is_same_v<B, float> || std::is_same_v<T, float>), float, // or, if one operand is float, the result is float
        typename std::conditional<(std::is_same_v<B, unsigned long long> || std::is_same_v<T, unsigned long long>), unsigned long long,
        typename std::conditional<(std::is_same_v<B, long long> || std::is_same_v<T, long long>), long long,
        typename std::conditional<(std::is_same_v<B, unsigned long> || std::is_same_v<T, unsigned long>), unsigned long,
        typename std::conditional<(std::is_same_v<B, long> || std::is_same_v<T, long>), typename std::conditional<(std::is_same_v<B, unsigned int> || std::is_same_v<T, unsigned int>), unsigned long, long>::type, // If the larger type is signed, but the other type is unsigned and larger than int, promote to unsigned
        typename std::conditional<(std::is_same_v<B, unsigned int> || std::is_same_v<T, unsigned int>), unsigned int,
        int // the default type for any smaller type is to result in int
        >::type>::type>::type>::type>::type>::type>::type;

    using LeftTypeButAtLeastInt_t = typename
        std::conditional<(std::is_same_v<B, unsigned long long>), unsigned long long,
        typename std::conditional<(std::is_same_v<B, long long>), long long,
        typename std::conditional<(std::is_same_v<B, unsigned long>), unsigned long,
        typename std::conditional<(std::is_same_v<B, long>), long,
        typename std::conditional<(std::is_same_v<B, unsigned int>), unsigned int,
        int // the default type for any smaller type is to result in int
        >::type>::type>::type>::type>::type;

    B ResultAssignment = 0;
    ResultType_t ResultOfOp = 0;
    LeftTypeButAtLeastInt_t ResultOfShift = 0;
    int ResultComparison = 0;
    bool isResultNonDet = false;
#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS
    if (TypeIsNonDeterministic(TopValue->Typ) && !TypeIsNonDeterministic(BottomValue->Typ) && CheckAndResolveVariable<T, B>(Parser, TopValue, BottomValue, Op)) {
        ExpressionPushT<int>(Parser, StackTop, 1, false);
    }
    else if (!TypeIsNonDeterministic(TopValue->Typ) && TypeIsNonDeterministic(BottomValue->Typ) && CheckAndResolveVariable<B, T>(Parser, BottomValue, TopValue, Op)) {
        ExpressionPushT<int>(Parser, StackTop, 1, false);
    }
    else {
#endif
        T Top = CoerceT<T>(TopValue);
        B Bot = CoerceT<B>(BottomValue);
        
        if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning using Infix (%s and %s) to %s from %s the value %.17g, Op = %s.\n", typeid(B).name(), typeid(T).name(), BottomValue->VarIdentifier, TopValue->VarIdentifier, Top, tokenToString(Op));
        }
        else if constexpr(std::is_unsigned_v<T>) {
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning using Infix (%s and %s) to %s from %s the value %llu, Op = %s.\n", typeid(B).name(), typeid(T).name(), BottomValue->VarIdentifier, TopValue->VarIdentifier, (unsigned long long)Top, tokenToString(Op));
        }
        else {
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning using Infix (%s and %s) to %s from %s the value %lli, Op = %s.\n", typeid(B).name(), typeid(T).name(), BottomValue->VarIdentifier, TopValue->VarIdentifier, (long long)Top, tokenToString(Op));
        }
#ifndef EXPR_TEMPLATE_VIA_ASSUMPTIONS
        isResultNonDet = PropagateAndResolveNonDeterminism(Parser, TopValue, BottomValue, Op);
#endif
        OpType opType = OpType::INVALID;
        switch (Op)
        {
        case TokenAssign:               ResultAssignment = AssignT<B>(Parser, BottomValue, Top, false); opType = OpType::ASSIGNMENT; break;
        case TokenAddAssign:            ResultAssignment = AssignT<B>(Parser, BottomValue, Bot + Top, false); opType = OpType::ASSIGNMENT; break;
        case TokenSubtractAssign:       ResultAssignment = AssignT<B>(Parser, BottomValue, Bot - Top, false); opType = OpType::ASSIGNMENT; break;
        case TokenMultiplyAssign:       ResultAssignment = AssignT<B>(Parser, BottomValue, Bot * Top, false); opType = OpType::ASSIGNMENT; break;
        case TokenDivideAssign:         ResultAssignment = AssignT<B>(Parser, BottomValue, Bot / Top, false); opType = OpType::ASSIGNMENT; break;
        case TokenEqual:                ResultComparison = handleEqualityCheck<B, T>(Bot, Top); opType = OpType::COMPARISON; break;
        case TokenNotEqual:             ResultComparison = Bot != Top; opType = OpType::COMPARISON; break;
        case TokenLessThan:             ResultComparison = Bot < Top; opType = OpType::COMPARISON; break;
        case TokenGreaterThan:          ResultComparison = Bot > Top; opType = OpType::COMPARISON; break;
        case TokenLessEqual:            ResultComparison = Bot <= Top; opType = OpType::COMPARISON; break;
        case TokenGreaterEqual:         ResultComparison = Bot >= Top; opType = OpType::COMPARISON; break;
        case TokenPlus:                 ResultOfOp = Bot + Top; opType = OpType::STANDARD; break;
        case TokenMinus:                ResultOfOp = Bot - Top; opType = OpType::STANDARD; break;
        case TokenAsterisk:             ResultOfOp = Bot * Top; opType = OpType::STANDARD; break;
        case TokenSlash:                ResultOfOp = Bot / Top; opType = OpType::STANDARD; break;
        default:
            if constexpr(!(std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<B, float> || std::is_same_v<B, double>)) {
                switch (Op) {
                case TokenModulusAssign:        ResultAssignment = AssignT<B>(Parser, BottomValue, Bot % Top, false); opType = OpType::ASSIGNMENT; break;
                case TokenShiftLeftAssign:      ResultAssignment = AssignT<B>(Parser, BottomValue, Bot << Top, false); opType = OpType::ASSIGNMENT; break;
                case TokenShiftRightAssign:     ResultAssignment = AssignT<B>(Parser, BottomValue, Bot >> Top, false); opType = OpType::ASSIGNMENT; break;
                case TokenArithmeticAndAssign:  ResultAssignment = AssignT<B>(Parser, BottomValue, Bot & Top, false); opType = OpType::ASSIGNMENT; break;
                case TokenArithmeticOrAssign:   ResultAssignment = AssignT<B>(Parser, BottomValue, Bot | Top, false); opType = OpType::ASSIGNMENT; break;
                case TokenArithmeticExorAssign: ResultAssignment = AssignT<B>(Parser, BottomValue, Bot ^ Top, false); opType = OpType::ASSIGNMENT; break;
                case TokenLogicalOr:            ResultComparison = Bot || Top; opType = OpType::COMPARISON; break;
                case TokenLogicalAnd:           ResultComparison = Bot && Top; opType = OpType::COMPARISON; break;
                case TokenArithmeticOr:         ResultOfOp = Bot | Top; opType = OpType::STANDARD; break;
                case TokenArithmeticExor:       ResultOfOp = Bot ^ Top; opType = OpType::STANDARD; break;
                case TokenAmpersand:            ResultOfOp = Bot & Top; opType = OpType::STANDARD; break;
                case TokenShiftLeft:            ResultOfShift = Bot << Top; opType = OpType::SHIFT; break;
                case TokenShiftRight:           ResultOfShift = Bot >> Top; opType = OpType::SHIFT; break;
                case TokenModulus:              ResultOfOp = Bot % Top; opType = OpType::STANDARD; break;
                default:
                    ProgramFail(Parser, "Invalid Operation '%s' on type %s and %s.", tokenToString(Op), typeid(B).name(), typeid(T).name()); break;
                }
            }
            else {
                ProgramFail(Parser, "Invalid Operation '%s' on type %s and %s.", tokenToString(Op), typeid(B).name(), typeid(T).name()); break;
            }
        }

        switch (opType) {
        case OpType::ASSIGNMENT: ExpressionPushT<B>(Parser, StackTop, ResultAssignment, isResultNonDet); break;
        case OpType::COMPARISON: ExpressionPushT<int>(Parser, StackTop, ResultComparison, isResultNonDet); break;
        case OpType::SHIFT: ExpressionPushT<LeftTypeButAtLeastInt_t>(Parser, StackTop, ResultOfShift, isResultNonDet); break;
        case OpType::STANDARD: ExpressionPushT<ResultType_t>(Parser, StackTop, ResultOfOp, isResultNonDet); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled operation type %i!", (int)opType); break;
        }
#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS
    }
#endif
}

void InfixOperationDispatcher(ParseState* Parser, Value* BottomValue, Value* TopValue, ExpressionStack** StackTop, LexToken const& Op) {
    switch (BottomValue->Typ->Base) {
    case BaseType::TypeInt:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<int, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<int, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<int, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<int, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<int, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<int, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<int, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<int, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<int, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<int, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<int, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<int, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeShort:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<short, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<short, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<short, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<short, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<short, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<short, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<short, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<short, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<short, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<short, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<short, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<short, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeChar:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<char, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<char, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<char, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<char, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<char, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<char, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<char, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<char, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<char, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<char, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<char, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<char, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeLong:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<long, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<long, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<long, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<long, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<long, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<long, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<long, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<long, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<long, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<long, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<long, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<long, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeLongLong:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<long long, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<long long, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<long long, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<long long, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<long long, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<long long, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<long long, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<long long, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<long long, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<long long, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<long long, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<long long, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeUnsignedInt:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<unsigned int, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<unsigned int, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<unsigned int, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<unsigned int, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<unsigned int, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<unsigned int, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<unsigned int, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<unsigned int, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<unsigned int, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<unsigned int, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<unsigned int, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<unsigned int, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeUnsignedShort:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<unsigned short, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<unsigned short, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<unsigned short, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<unsigned short, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<unsigned short, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<unsigned short, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<unsigned short, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<unsigned short, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<unsigned short, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<unsigned short, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<unsigned short, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<unsigned short, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeUnsignedChar:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<unsigned char, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<unsigned char, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<unsigned char, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<unsigned char, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<unsigned char, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<unsigned char, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<unsigned char, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<unsigned char, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<unsigned char, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<unsigned char, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<unsigned char, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<unsigned char, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeUnsignedLong:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<unsigned long, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<unsigned long, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<unsigned long, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<unsigned long, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<unsigned long, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<unsigned long, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<unsigned long, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<unsigned long, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<unsigned long, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<unsigned long, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<unsigned long, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<unsigned long, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeUnsignedLongLong:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<unsigned long long, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<unsigned long long, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<unsigned long long, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<unsigned long long, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<unsigned long long, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<unsigned long long, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<unsigned long long, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<unsigned long long, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<unsigned long long, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<unsigned long long, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<unsigned long long, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<unsigned long long, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeFloat:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<float, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<float, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<float, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<float, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<float, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<float, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<float, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<float, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<float, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<float, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<float, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<float, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    case BaseType::TypeDouble:
        switch (TopValue->Typ->Base) {
        case BaseType::TypeInt: InfixOperation<double, int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: InfixOperation<double, short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeChar: InfixOperation<double, char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: InfixOperation<double, long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: InfixOperation<double, long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: InfixOperation<double, unsigned int>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: InfixOperation<double, unsigned short>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: InfixOperation<double, unsigned char>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: InfixOperation<double, unsigned long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: InfixOperation<double, unsigned long long>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: InfixOperation<double, float>(Parser, BottomValue, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: InfixOperation<double, double>(Parser, BottomValue, TopValue, StackTop, Op); break;
        default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of TopValue in InfixOperation!", getType(TopValue)); break;
        }
        break;
    default: ProgramFail(Parser, "Internal Error: Unhandled base type %s of BottomValue in InfixOperation!", getType(BottomValue)); break;
    }
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
    debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value with type %s and value %li or %.17g.\n", getType(PushValue), GET_VALUE(PushValue), IS_FP(PushValue) ? GET_FP_VALUE(PushValue) : 0.0);
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

    ValueLoc = VariableAllocValueFromExistingData(Parser, DerefType, (union AnyValue *) DerefDataLoc, DerefIsLValue, DerefVal, nullptr);
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

/* assign to a pointer */
void ExpressionAssignToPointer(struct ParseState *Parser, Value *ToValue, Value *FromValue, const char *FuncName, int ParamNo, int AllowPointerCoercion)
{
    struct ValueType *PointedToType = ToValue->Typ->FromType;
    debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to pointer %s, Type = %s.\n", ToValue->VarIdentifier, getType(PointedToType));

    if (FromValue->Typ->Base == ToValue->Typ->Base || FromValue->Typ == Parser->pc->VoidPtrType
        || (ToValue->Typ == Parser->pc->VoidPtrType && FromValue->Typ->Base == BaseType::TypePointer)
//        || (FromValue->Typ->FromType != nullptr && TypeIsNonDeterministic(FromValue->Typ->FromType)
//            && ToValue->Typ->FromType->Base == FromValue->Typ->FromType->Base && FromValue->Typ->Base != BaseType::TypeArray)
        )
    {
        ToValue->Val->Pointer = FromValue->Val->Pointer;      /* plain old pointer assignment */
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to pointer, case A.\n");
    }

    else if (FromValue->Typ->Base == BaseType::TypeArray && (PointedToType->Base == FromValue->Typ->FromType->Base || ToValue->Typ == Parser->pc->VoidPtrType))
    {
        /* the form is: blah *x = array of blah */
        ToValue->Val->Pointer = (void *)&FromValue->Val->ArrayMem[0];
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to pointer, case B.\n");
    }
    else if (FromValue->Typ->Base == BaseType::TypePointer && FromValue->Typ->FromType->Base == BaseType::TypeArray &&
               (PointedToType == FromValue->Typ->FromType->FromType || ToValue->Typ == Parser->pc->VoidPtrType) )
    {
        /* the form is: blah *x = pointer to array of blah */
        ToValue->Val->Pointer = VariableDereferencePointer(Parser, FromValue, nullptr, nullptr, nullptr, nullptr);
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to pointer, case C.\n");
    }
    else if (IS_NUMERIC_COERCIBLE(FromValue) && CoerceT<long long>(FromValue) == 0)
    {
        /* null pointer assignment */
        ToValue->Val->Pointer = nullptr;
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to pointer, case nullptr.\n");
    }
    else if (AllowPointerCoercion && IS_NUMERIC_COERCIBLE(FromValue))
    {
        /* assign integer to native pointer */
        ToValue->Val->Pointer = (void *)(unsigned long)CoerceT<unsigned long long>(FromValue);
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to pointer, case int to ptr.\n");
    }
    else if (AllowPointerCoercion && FromValue->Typ->Base == BaseType::TypePointer)
    {
        /* assign a pointer to a pointer to a different type */
        ToValue->Val->Pointer = FromValue->Val->Pointer;
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to pointer, case D.\n");
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

#ifndef EXPR_TEMPLATE_VIA_ASSUMPTIONS
    // if Source is NonDet, so should the assigned value
    if (TypeIsNonDeterministic(SourceValue->Typ)) {
        if (!TypeIsNonDeterministic(DestValue->Typ)) {
#ifdef VERBOSE
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Variable %s of type %s is becoming NonDet by assignment from %s.\n", DestValue->VarIdentifier, getType(DestValue), SourceValue->VarIdentifier);
#endif
            DestValue->Typ = TypeGetNonDeterministic(Parser, DestValue->Typ);
        }
#ifdef VERBOSE
        else {
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Variable %s would become NonDet by assignment from %s, but was already.\n", DestValue->VarIdentifier, SourceValue->VarIdentifier);
        }
#endif
    }
    else {
        // SourceValue is deterministic
        if (TypeIsNonDeterministic(DestValue->Typ)) {
#ifdef VERBOSE
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Variable %s of type %s is becoming Det by assignment from %s.\n", DestValue->VarIdentifier, getType(DestValue), SourceValue->VarIdentifier);
#endif
            DestValue->Typ = TypeGetDeterministic(Parser, DestValue->Typ);
        }
#ifdef VERBOSE
        else {
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Variable %s would become Det by assignment from %s, but was already.\n", DestValue->VarIdentifier, SourceValue->VarIdentifier);
        }
#endif
    }

    if (DestValue->LValueFrom != nullptr){
        if (TypeIsNonDeterministic(SourceValue->Typ)) {
            if (!TypeIsNonDeterministic(DestValue->LValueFrom->Typ)) {
                DestValue->LValueFrom->Typ = TypeGetNonDeterministic(Parser, DestValue->LValueFrom->Typ);
#ifdef VERBOSE
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Variable %s is becoming NonDet by assignment from %s (LValue).\n", DestValue->LValueFrom->VarIdentifier, SourceValue->VarIdentifier);
#endif
            }
#ifdef VERBOSE
            else {
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Variable %s would become NonDet by assignment from %s, but was already (LValue).\n", DestValue->LValueFrom->VarIdentifier, SourceValue->VarIdentifier);
            }
#endif
        } else {
            // SourceValue is deterministic
            if (TypeIsNonDeterministic(DestValue->LValueFrom->Typ)) {
                DestValue->LValueFrom->Typ = TypeGetDeterministic(Parser, DestValue->LValueFrom->Typ);
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Variable %s is becoming Det by assignment from %s.\n", DestValue->VarIdentifier, SourceValue->VarIdentifier);

                // We might need to "resolve" array NonDet here
                if (DestValue->ArrayRoot != nullptr && DestValue->ArrayIndex != -1) {
                    int i = DestValue->ArrayIndex;
                    Value* a = DestValue->ArrayRoot;
                    if (a->Typ->NDListSize > 0) {
                        if (i >= a->Typ->NDListSize) {
                            ProgramFail(Parser, "array index out of bounds (%d >= %d) during NonDet resolution from expr assignment", i, a->Typ->NDListSize);
                        }
                        setNonDetListElement(a->Typ->NDList, i, false);
#ifdef VERBOSE
                        debugf(EXPR_TEMPLATE_STRING_PREFIX "Resolved NonDet of array entry %i by expr assignment.\n", i);
#endif
                    }
                }
            }
        }
    }
    debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to variable %s, Type = %s.\n", DestValue->VarIdentifier, getType(DestValue));

    // check if not const
    if (DestValue->ConstQualifier == TRUE){
        switch (DestValue->Typ->Base){
            case BaseType::TypeInt:
            case BaseType::TypeShort:
            case BaseType::TypeChar:
            case BaseType::TypeLong:
            case BaseType::TypeLongLong:
            case BaseType::TypeUnsignedInt:
            case BaseType::TypeUnsignedShort:
            case BaseType::TypeUnsignedLong:
            case BaseType::TypeUnsignedLongLong:
            case BaseType::TypeUnsignedChar:
            case BaseType::TypeDouble:
            case BaseType::TypeStruct:
            case BaseType::TypeUnion:
                AssignFail(Parser, "const %t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);
                break;
            default:
                break;
        }
    }
#endif

    // assign SourceValue by DestType
    switch (DestValue->Typ->Base)
    {
        case BaseType::TypeInt:              DestValue->Val->Integer = EXPR_TEMPLATE_PREFIX CoerceT<int>(SourceValue); break;
        case BaseType::TypeShort:            DestValue->Val->ShortInteger = EXPR_TEMPLATE_PREFIX CoerceT<short>(SourceValue); break;
        case BaseType::TypeChar:             DestValue->Val->Character = EXPR_TEMPLATE_PREFIX CoerceT<char>(SourceValue); break;
        case BaseType::TypeLong:             DestValue->Val->LongInteger = EXPR_TEMPLATE_PREFIX CoerceT<long>(SourceValue); break;
        case BaseType::TypeUnsignedInt:      DestValue->Val->UnsignedInteger = EXPR_TEMPLATE_PREFIX CoerceT<unsigned int>(SourceValue); break;
        case BaseType::TypeUnsignedShort:    DestValue->Val->UnsignedShortInteger = EXPR_TEMPLATE_PREFIX CoerceT<unsigned short>(SourceValue); break;
        case BaseType::TypeUnsignedLong:     DestValue->Val->UnsignedLongInteger = EXPR_TEMPLATE_PREFIX CoerceT<unsigned long>(SourceValue); break;
        case BaseType::TypeUnsignedChar:     DestValue->Val->UnsignedCharacter = EXPR_TEMPLATE_PREFIX CoerceT<unsigned char>(SourceValue); break;
        case BaseType::TypeLongLong:         DestValue->Val->LongLongInteger = EXPR_TEMPLATE_PREFIX CoerceT<long long>(SourceValue); break;
        case BaseType::TypeUnsignedLongLong: DestValue->Val->UnsignedLongLongInteger = EXPR_TEMPLATE_PREFIX CoerceT<unsigned long long>(SourceValue); break;
        case BaseType::TypeDouble:
            if (!IS_NUMERIC_COERCIBLE_PLUS_POINTERS(SourceValue, AllowPointerCoercion)) {
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);
            }

            DestValue->Val->Double = EXPR_TEMPLATE_PREFIX CoerceT<double>(SourceValue);
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning value %.17g as double.\n", DestValue->Val->Double);
            break;
        case BaseType::TypeFloat:
            if (!IS_NUMERIC_COERCIBLE_PLUS_POINTERS(SourceValue, AllowPointerCoercion)) {
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);
            }

            DestValue->Val->Float = EXPR_TEMPLATE_PREFIX CoerceT<float>(SourceValue);
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning value %.17g as float.\n", DestValue->Val->Float);
            break;
        case BaseType::TypePointer:
            ExpressionAssignToPointer(Parser, DestValue, SourceValue, FuncName, ParamNo, AllowPointerCoercion);
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning value as pointer.\n");
            break;

        case BaseType::TypeArray:
            if (SourceValue->Typ->Base == BaseType::TypeArray && DestValue->Typ->ArraySize == 0)
            {
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to unsized array.\n");
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
            if (DestValue->Typ->FromType->Base == BaseType::TypeChar && SourceValue->Typ->Base == BaseType::TypePointer && SourceValue->Typ->FromType->Base == BaseType::TypeChar)
            {
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to array, case A.\n");
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
                && DestValue->Typ->FromType->Base != SourceValue->Typ->FromType->Base) { // FIXME : comparing array type is not safe this way
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);
            }

            if (DestValue->Typ->ArraySize != SourceValue->Typ->ArraySize) {
                AssignFail(Parser, "from an array of size %d to one of size %d", nullptr, nullptr, DestValue->Typ->ArraySize, SourceValue->Typ->ArraySize, FuncName, ParamNo);
            }

//            memcpy((void *)DestValue->Val, (void *)SourceValue->Val, TypeSizeValue(DestValue, FALSE));
            // Instead of copying just point to same memory. Like in C!
            DestValue->Val = SourceValue->Val;
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to array.\n");
            break;

        case BaseType::TypeStruct:
        case BaseType::TypeUnion:
            if (DestValue->Typ != SourceValue->Typ) {
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);
            }

            memcpy((void *)DestValue->Val, (void *)SourceValue->Val, TypeSizeValue(SourceValue, FALSE));
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to struct/union.\n");
            break;
        case BaseType::TypeFunctionPtr:
            if (IS_NUMERIC_COERCIBLE(SourceValue) && CoerceT<long long>(SourceValue) == 0){
                DestValue->Val->Identifier = nullptr;
                break;
            }
            if (DestValue->Typ != SourceValue->Typ) {
                AssignFail(Parser, "%t from %t", DestValue->Typ, SourceValue->Typ, 0, 0, FuncName, ParamNo);
            }

            DestValue->Val->Identifier = SourceValue->Val->Identifier;
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Assigning to function pointer, case A.\n");
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
    if (!IS_NUMERIC_COERCIBLE(TopValue)) {
        ProgramFail(Parser, "first argument to '?' should be a number");
    }

    if (CoerceT<int>(TopValue))
    {
        /* the condition's true, return the TopValue */
#ifndef EXPR_TEMPLATE_VIA_ASSUMPTIONS
        nitwit::parse::ConditionCallback(Parser, true);
#endif
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value from QuestionMarkOperator.\n");
        ExpressionStackPushValue(Parser, StackTop, TopValue);
    }
    else
    {
        /* the condition's false, return void */
#ifndef EXPR_TEMPLATE_VIA_ASSUMPTIONS
        nitwit::parse::ConditionCallback(Parser, false);
#endif
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value by type from QuestionMarkOperator.\n");
        ExpressionStackPushValueByType(Parser, StackTop, &Parser->pc->VoidType);
    }
}

/* evaluate the second half of a ternary operator x ? y : z */
void ExpressionColonOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, Value *BottomValue, Value *TopValue)
{
    if (TopValue->Typ->Base == BaseType::TypeVoid)
    {
        /* invoke the "else" part - return the BottomValue */
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value from ColonOperator (else).\n");
        ExpressionStackPushValue(Parser, StackTop, BottomValue);
    }
    else
    {
        /* it was a "then" - return the TopValue */
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value from ColonOperator (then).\n");
        ExpressionStackPushValue(Parser, StackTop, TopValue);
    }
}

/* evaluate a prefix operator */
void ExpressionPrefixOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Op, Value *TopValue)
{
    Value *Result;
    union AnyValue *ValPtr;

    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionPrefixOperator(%s)\n", tokenToString(Op));
    switch (Op)
    {
        case TokenAmpersand:
            if (!TopValue->IsLValue)
                ProgramFail(Parser, "can't get the address of this");

            if (TopValue->Typ->Base == BaseType::TypeFunctionPtr) {
                char * id = TopValue->Val->Identifier;
                Result = VariableAllocValueFromType(Parser->pc, Parser, TopValue->Typ, FALSE, nullptr, FALSE);
                Result->Val->Identifier = id;
            } else {
                ValPtr = TopValue->Val;
                Result = VariableAllocValueFromType(Parser->pc, Parser, TypeGetMatching(Parser->pc, Parser, TopValue->Typ, BaseType::TypePointer, 0, Parser->pc->StrEmpty, TRUE, nullptr), FALSE, TopValue->LValueFrom, FALSE);
                Result->Val->Pointer = (void *)ValPtr;
            }
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value from PrefixOperator (TokenAmpersand).\n");
            ExpressionStackPushValueNode(Parser, StackTop, Result);
            break;
        case TokenAsterisk:
            if (TopValue->Typ->Base == BaseType::TypeFunctionPtr || (TopValue->Typ->Base == BaseType::TypeArray
                    && TopValue->Typ->FromType->Base == BaseType::TypeFunctionPtr)) {
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value from PrefixOperator (TokenAsterisk).\n");
                ExpressionStackPushValue(Parser, StackTop, TopValue);
                break;
            }
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing dereference from PrefixOperator (TokenAsterisk).\n");
            ExpressionStackPushDereference(Parser, StackTop, TopValue);
            break;

        case TokenSizeof:
            /* return the size of the argument */
            if (TopValue->Typ->Base == BaseType::Type_Type) {
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing long long from PrefixOperator (TokenSizeof, then).\n");
                ExpressionPushLongLong(Parser, StackTop, TypeSize(TopValue->Val->Typ, TopValue->Val->Typ->ArraySize, TRUE));
            }
            else {
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing long long from PrefixOperator (TokenSizeof, else).\n");
                ExpressionPushLongLong(Parser, StackTop, TypeSize(TopValue->Typ, TopValue->Typ->ArraySize, TRUE));
            }
            break;

        default:
            /* an arithmetic operator */
            if (TopValue->Typ->Base == BaseType::TypePointer)
            {
                /* pointer prefix arithmetic */
                int Size = TypeSize(TopValue->Typ->FromType, 0, TRUE);
                Value* StackValue;
                void* ResultPtr = nullptr;

                if (TopValue->Val->Pointer == nullptr && Op != TokenUnaryNot)
                    ProgramFail(Parser, "invalid use of a nullptr pointer");

                if (!TopValue->IsLValue)
                    ProgramFail(Parser, "can't assign to this");

                switch (Op)
                {
                case TokenIncrement:    TopValue->Val->Pointer = (void*)((char*)TopValue->Val->Pointer + Size);
                    ResultPtr = TopValue->Val->Pointer; break;
                case TokenDecrement:    TopValue->Val->Pointer = (void*)((char*)TopValue->Val->Pointer - Size);
                    ResultPtr = TopValue->Val->Pointer; break;
                case TokenUnaryNot:     ResultPtr = reinterpret_cast<void*>(!TopValue->Val->Pointer); break;
                default:                ProgramFail(Parser, "Invalid Operation '%s' on pointer.", tokenToString(Op)); break;
                }

                //                ResultPtr = TopValue->Val->Pointer;
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value byt type from PrefixOperator (Pointer).");
                StackValue = ExpressionStackPushValueByType(Parser, StackTop, TopValue->Typ);
                StackValue->Val->Pointer = ResultPtr;
            }
            else if (IS_NUMERIC_COERCIBLE(TopValue) || TopValue->Typ->Base == BaseType::TypeDouble || TopValue->Typ->Base == BaseType::TypeFloat)
            {
                /* integer/fp prefix arithmetic */
                switch (TopValue->Typ->Base) {
                case BaseType::TypeChar: PrefixOperation<char>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeUnsignedChar: PrefixOperation<unsigned char>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeShort: PrefixOperation<short>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeUnsignedShort: PrefixOperation<unsigned short>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeInt: PrefixOperation<int>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeUnsignedInt: PrefixOperation<unsigned int>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeLong: PrefixOperation<long>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeUnsignedLong: PrefixOperation<unsigned long>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeLongLong: PrefixOperation<long long>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeUnsignedLongLong: PrefixOperation<unsigned long long>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeFloat: PrefixOperation<float>(Parser, TopValue, StackTop, Op); break;
                case BaseType::TypeDouble: PrefixOperation<double>(Parser, TopValue, StackTop, Op); break;
                default:
                    ProgramFail(Parser, "Internal Error: Unhandled Type of integer/fp prefix operation: %s.", getType(TopValue)); break;
                }
            }
            else {
                ProgramFail(Parser, "Invalid Operation '%s' on unknown/unhandled type %s.", tokenToString(Op), getType(TopValue));
            }
            break;
    }
}

/* evaluate a postfix operator */
void ExpressionPostfixOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Op, Value *TopValue)
{
    debugf("ExpressionPostfixOperator(%s)\n", tokenToString(Op));
    if (Op == TokenQuestionMark) {
        ExpressionQuestionMarkOperator(Parser, StackTop, TopValue);
    }
    else if (IS_NUMERIC_COERCIBLE(TopValue) || TopValue->Typ->Base == BaseType::TypeDouble || TopValue->Typ->Base == BaseType::TypeFloat)
    {
        /* integer/fp postfix arithmetic */
        switch (TopValue->Typ->Base) {
        case BaseType::TypeChar: PostfixOperation<char>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedChar: PostfixOperation<unsigned char>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeShort: PostfixOperation<short>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedShort: PostfixOperation<unsigned short>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeInt: PostfixOperation<int>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedInt: PostfixOperation<unsigned int>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeLong: PostfixOperation<long>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLong: PostfixOperation<unsigned long>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeLongLong: PostfixOperation<long long>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeUnsignedLongLong: PostfixOperation<unsigned long long>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeFloat: PostfixOperation<float>(Parser, TopValue, StackTop, Op); break;
        case BaseType::TypeDouble: PostfixOperation<double>(Parser, TopValue, StackTop, Op); break;
        default:
            ProgramFail(Parser, "Internal Error: Unhandled Type of integer/fp postfix operation: %s.", getType(TopValue)); break;
        }
    }
    else if (TopValue->Typ->Base == BaseType::TypePointer)
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

        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value by type from PostfixOperator (Pointer).");
        StackValue = ExpressionStackPushValueByType(Parser, StackTop, TopValue->Typ);
        StackValue->Val->Pointer = OrigPointer;
    }
    else {
        ProgramFail(Parser, "Invalid Operation '%s' on unknown/unhandled type %s.", tokenToString(Op), getType(TopValue));
    }
}

#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS
void ResolvedVariable(struct ParseState* Parser, const char* Identifier, Value* VariableValue) {
    auto* vl = static_cast<ValueList*>(malloc(sizeof(ValueList)));
    vl->Identifier = Identifier;
    vl->Next = Parser->ResolvedNonDetVars;
    Parser->ResolvedNonDetVars = vl;

#ifdef VERBOSE
    if (IS_FP(VariableValue)) {
        double fp = CoerceT<double>(VariableValue);
        cw_verbose(EXPR_TEMPLATE_STRING_PREFIX "Resolved var: %s: ---> %.17g\n", Identifier, fp);
    }
    else {
        long long i = CoerceT<long long>(VariableValue);
        cw_verbose(EXPR_TEMPLATE_STRING_PREFIX "Resolved var: %s: ---> %lli\n", Identifier, i);
    }
#endif

    VariableValue->Typ = TypeGetDeterministic(Parser, VariableValue->Typ);
}
#endif

/* evaluate an infix operator */
void ExpressionInfixOperator(struct ParseState *Parser, struct ExpressionStack **StackTop, enum LexToken Op, Value *BottomValue, Value *TopValue)
{
    Value *StackValue;
    void *Pointer;

    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionInfixOperator(%s)\n", tokenToString(Op));
    if (BottomValue == nullptr || TopValue == nullptr)
        ProgramFail(Parser, "invalid expression");

#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS
    if (BottomValue->Val == nullptr || TopValue->Val == nullptr) {
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing 0 as BottomValue or TopValue are nullptr.\n");
        ExpressionPushLongLong(Parser, StackTop, 0);
        return;
    }
#endif

    if (Op == TokenLeftSquareBracket)
    {
        /* array index */
        long long ArrayIndex;
        Value *Result = nullptr;

        if (!IS_NUMERIC_COERCIBLE(TopValue)) {
            ProgramFail(Parser, "array index must be an integer");
        }

        ArrayIndex = CoerceT<long long>(TopValue);

        // set nondet value for BottomValue when it is a nondet array
        if (BottomValue->Typ->NDList != nullptr) {
            if (ArrayIndex >= BottomValue->Typ->NDListSize) {
                ProgramFail(Parser, "array index out of bounds (%d >= %d)", ArrayIndex, BottomValue->Typ->NDListSize);
            }
            BottomValue->Typ->IsNonDet = getNonDetListElement(BottomValue->Typ->NDList, ArrayIndex);
#ifdef VERBOSE
            cw_verbose("Propagating NonDet to array value %s at index %lli.\n", BottomValue->VarIdentifier, ArrayIndex);
#endif
        }

        /* make the array element result */
        switch (BottomValue->Typ->Base)
        {
            case BaseType::TypeArray:
                Result = VariableAllocValueFromExistingData(Parser,
                                  TypeIsNonDeterministic(BottomValue->Typ) ? TypeGetNonDeterministic(Parser, BottomValue->Typ->FromType) : BottomValue->Typ->FromType,
                                  (union AnyValue *) (
                                          &BottomValue->Val->ArrayMem[0] +
                                          TypeSize(BottomValue->Typ, ArrayIndex,
                                                   TRUE)),
                                  BottomValue->IsLValue,
                                  BottomValue->LValueFrom, nullptr); 
#ifdef VERBOSE
                cw_verbose("Accessing array %s at index %lli, IsLValue: %i, LValueFrom != nullptr: %i.\n", BottomValue->VarIdentifier, ArrayIndex, (BottomValue->IsLValue) ? 1 : 0, (BottomValue->LValueFrom != nullptr) ? 1 : 0);
#endif
                Result->ArrayRoot = BottomValue->LValueFrom;
                Result->ArrayIndex = ArrayIndex;
                break;

            case BaseType::TypePointer: Result = VariableAllocValueFromExistingData(Parser, BottomValue->Typ->FromType,
                                  (union AnyValue *) (
                                          (char *) BottomValue->Val->Pointer +
                                          TypeSize(BottomValue->Typ->FromType,
                                                   0, TRUE) * ArrayIndex),
                                  BottomValue->LValueFrom != nullptr ? TRUE: BottomValue->IsLValue,
                                  BottomValue->LValueFrom, nullptr);
                Result->ArrayRoot = BottomValue->LValueFrom;
                Result->ArrayIndex = ArrayIndex;
#ifdef VERBOSE
                cw_verbose(EXPR_TEMPLATE_STRING_PREFIX "Accessing pointer %s at index %lli, IsLValue: %i, LValueFrom != nullptr: %i.\n", BottomValue->VarIdentifier, ArrayIndex, (BottomValue->IsLValue) ? 1 : 0, (BottomValue->LValueFrom != nullptr) ? 1 : 0);
#endif
                break;
                                  // allow to change to LValue later ((unsigned*)&dmax)[0] = 0x47efffff;
            default:          ProgramFail(Parser, "this %t is not an array", BottomValue->Typ);
        }

        /* push new value node, no value set yet */
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value from PostfixOperator (LeftSquareBracket).");
        ExpressionStackPushValueNode(Parser, StackTop, Result);
    }

    else if (Op == TokenColon) {
        ExpressionColonOperator(Parser, StackTop, TopValue, BottomValue);
    }
    else if ( (IS_FP(TopValue) || IS_NUMERIC_COERCIBLE(TopValue)) &&
              (IS_FP(BottomValue) || IS_NUMERIC_COERCIBLE(BottomValue)) )
    {
        InfixOperationDispatcher(Parser, BottomValue, TopValue, StackTop, Op);
    }
#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS
    else if (BottomValue->Typ->Base == BaseType::TypeFunctionPtr && TopValue->Typ->Base == BaseType::TypeFunctionPtr) {
        ExpressionPushT<int>(Parser, StackTop, BottomValue->Val->Identifier == TopValue->Val->Identifier, TypeIsNonDeterministic(BottomValue->Typ) || TypeIsNonDeterministic(TopValue->Typ));
    }
#endif
    else if (BottomValue->Typ->Base == BaseType::TypePointer && IS_NUMERIC_COERCIBLE(TopValue))
    {
        /* pointer/integer infix arithmetic */
        bool const isResultNonDet = TypeIsNonDeterministic(BottomValue->Typ) || TypeIsNonDeterministic(TopValue->Typ);
        long long TopInt = CoerceT<long long>(TopValue);

        if (Op == TokenEqual || Op == TokenNotEqual)
        {
            /* comparison to a nullptr pointer */
            if (TopInt != 0) {
                ProgramFail(Parser, "invalid operation");
            }

            if (Op == TokenEqual) {
                ExpressionPushT<int>(Parser, StackTop, BottomValue->Val->Pointer == nullptr, isResultNonDet);
            }
            else {
                ExpressionPushT<int>(Parser, StackTop, BottomValue->Val->Pointer != nullptr, isResultNonDet);
            }
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

            debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value by type from InfixOperator (+ or -).");
            StackValue = ExpressionStackPushValueByType(Parser, StackTop, BottomValue->Typ);
            StackValue->Val->Pointer = Pointer;
        }
        else if (Op == TokenAssign && TopInt == 0)
        {
            /* assign a nullptr pointer */
            HeapUnpopStack(Parser->pc, sizeof(Value));
            EXPR_TEMPLATE_PREFIX ExpressionAssign(Parser, BottomValue, TopValue, FALSE, nullptr, 0, FALSE);
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value from InfixOperator (Assign).");
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
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value from InfixOperator (+ or - Assign).");
            ExpressionStackPushValueNode(Parser, StackTop, BottomValue);
        }
        else {
            ProgramFail(Parser, "Invalid Operation '%s' on pointer.", tokenToString(Op));
        }
    } else if (BottomValue->Typ->Base == BaseType::TypeFunctionPtr && IS_NUMERIC_COERCIBLE(TopValue))
    {
        /* pointer/integer infix arithmetic */
        bool const isResultNonDet = TypeIsNonDeterministic(BottomValue->Typ) || TypeIsNonDeterministic(TopValue->Typ);
        long long TopInt = CoerceT<long long>(TopValue);

        if (Op == TokenEqual || Op == TokenNotEqual)
        {
            /* comparison to a nullptr pointer */
            if (TopInt != 0) {
                ProgramFail(Parser, "Invalid Operation '%s' on nullptr.", tokenToString(Op));
            }

            if (Op == TokenEqual) {
                ExpressionPushT<int>(Parser, StackTop, BottomValue->Val->Identifier == nullptr, isResultNonDet);
            }
            else {
                ExpressionPushT<int>(Parser, StackTop, BottomValue->Val->Identifier != nullptr, isResultNonDet);
            }
        }
        else if (Op == TokenAssign && TopInt == 0)
        {
            /* checking if func ptr is a nullptr pointer (allow usage of assigns instead of equal) */
            HeapUnpopStack(Parser->pc, sizeof(Value));
            ExpressionPushT<int>(Parser, StackTop, BottomValue->Val->Identifier == nullptr, isResultNonDet);
        }
        else {
            ProgramFail(Parser, "Invalid Operation '%s' on function pointer.", tokenToString(Op));
        }
    }
    else if (BottomValue->Typ->Base == BaseType::TypePointer && TopValue->Typ->Base == BaseType::TypePointer && Op != TokenAssign)
    {
        /* pointer/pointer operations */
        bool const isResultNonDet = TypeIsNonDeterministic(BottomValue->Typ) || TypeIsNonDeterministic(TopValue->Typ);
        char *TopLoc = (char *)TopValue->Val->Pointer;
        char *BottomLoc = (char *)BottomValue->Val->Pointer;

        switch (Op)
        {
            case TokenEqual:
                ExpressionPushT<int>(Parser, StackTop, BottomLoc == TopLoc, isResultNonDet); break;
            case TokenNotEqual:
                ExpressionPushT<int>(Parser, StackTop, BottomLoc != TopLoc, isResultNonDet); break;
            case TokenLessEqual:
                ExpressionPushT<int>(Parser, StackTop, BottomLoc <= TopLoc, isResultNonDet); break;
            case TokenLessThan:
                ExpressionPushT<int>(Parser, StackTop, BottomLoc < TopLoc, isResultNonDet); break;
            case TokenGreaterEqual:
                ExpressionPushT<int>(Parser, StackTop, BottomLoc >= TopLoc, isResultNonDet); break;
            case TokenGreaterThan:
                ExpressionPushT<int>(Parser, StackTop, BottomLoc > TopLoc, isResultNonDet); break;
            case TokenMinus:
                ExpressionPushT<int>(Parser, StackTop, BottomLoc - TopLoc, isResultNonDet); break;
            default:
                ProgramFail(Parser, "Invalid Operation '%s' on pointers.", tokenToString(Op)); break;
        }
    }
    else if (Op == TokenAssign)
    {
        /* assign a non-numeric type */
        HeapUnpopStack(Parser->pc, MEM_ALIGN(sizeof(Value)));   /* XXX - possible bug if lvalue is a temp value and takes more than sizeof(Value) */
        EXPR_TEMPLATE_PREFIX ExpressionAssign(Parser, BottomValue, TopValue, FALSE, nullptr, 0, FALSE);
        if (BottomValue->Typ->Base == BaseType::TypeFunctionPtr) {
            bool const isResultNonDet = TypeIsNonDeterministic(BottomValue->Typ) || TypeIsNonDeterministic(TopValue->Typ);
            ExpressionPushT<int>(Parser, StackTop, !strcmp(BottomValue->Val->Identifier, TopValue->Val->Identifier), isResultNonDet);
        } else {
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value node from InfixOperator (TokenAssign).\n");
            ExpressionStackPushValueNode(Parser, StackTop, BottomValue);
        }
    }
    else if (Op == TokenCast)
    {
        /* cast a value to a different type */   /* XXX - possible bug if the destination type takes more than sizeof(Value) + sizeof(struct ValueType *) */
        Value *ValueLoc = ExpressionStackPushValueByType(Parser, StackTop, BottomValue->Val->Typ);
        EXPR_TEMPLATE_PREFIX ExpressionAssign(Parser, ValueLoc, TopValue, TRUE, nullptr, 0, TRUE);
        ValueLoc->LValueFrom = TopValue->LValueFrom; // allow things like ((unsigned*)&dmax)[0] = 0x47efffff;
    } else if (Op == TokenOpenBracket) {
        // called a function
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value node from InfixOperator (TokenOpenBracket).\n");
        ExpressionStackPushValue(Parser, StackTop, TopValue);
    } else if ((BottomValue->Typ->Base == BaseType::TypeArray && IS_INTEGER_NUMERIC(TopValue))
            || (TopValue->Typ->Base == BaseType::TypeArray && IS_INTEGER_NUMERIC(BottomValue))
//            || (BottomValue->Typ->Base == BaseType::TypePointer && IS_INTEGER_NUMERIC(TopValue))
//            || (TopValue->Typ->Base == BaseType::TypePointer && IS_INTEGER_NUMERIC(BottomValue))
            ) {

        Value *ArrayValue = BottomValue->Typ->Base == BaseType::TypeArray || BottomValue->Typ->Base == BaseType::TypePointer ? BottomValue : TopValue;
        long long ArrayIndex = BottomValue->Typ->Base != BaseType::TypeArray && BottomValue->Typ->Base != BaseType::TypePointer
                    ? CoerceT<long long>(BottomValue) : CoerceT<long long>(TopValue);

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
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value node by type from InfixOperator (+ or -).");
        StackValue = ExpressionStackPushValueByType(Parser, StackTop,
                                                    TypeGetMatching(Parser->pc, Parser, ArrayValue->Typ->FromType,
                                                                    BaseType::TypePointer, 0, Parser->pc->StrEmpty,
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

    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionStackCollapse(%d):\n", Precedence);
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
    while (TopStackNode != nullptr && TopStackNode->Next != nullptr && FoundPrecedence >= Precedence)
    {
        /* find the top operator on the stack */
        if (TopStackNode->Order == OperatorOrder::OrderNone)
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
                case OperatorOrder::OrderPrefix:
                    /* prefix evaluation */
                    debugf(EXPR_TEMPLATE_STRING_PREFIX "prefix evaluation\n");
                    TopValue = TopStackNode->Val;

                    /* pop the value and then the prefix operator - assume they'll still be there until we're done */
                    HeapPopStack(Parser->pc, nullptr, MEM_ALIGN(sizeof(struct ExpressionStack)) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(TopValue));
                    HeapPopStack(Parser->pc, TopOperatorNode, MEM_ALIGN(sizeof(struct ExpressionStack)));
                    *StackTop = TopOperatorNode->Next;

                    /* do the prefix operation */
                    if (Parser->Mode == RunMode::RunModeRun && FoundPrecedence < *IgnorePrecedence )
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

                case OperatorOrder::OrderPostfix:
                    /* postfix evaluation */
                    debugf(EXPR_TEMPLATE_STRING_PREFIX "postfix evaluation\n");
                    TopValue = TopStackNode->Next->Val;

                    /* pop the postfix operator and then the value - assume they'll still be there until we're done */
                    HeapPopStack(Parser->pc, nullptr, MEM_ALIGN(sizeof(struct ExpressionStack)));
                    HeapPopStack(Parser->pc, TopValue, MEM_ALIGN(sizeof(struct ExpressionStack)) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(TopValue));
                    *StackTop = TopStackNode->Next->Next;

                    /* do the postfix operation */
                    if (Parser->Mode == RunMode::RunModeRun && FoundPrecedence < *IgnorePrecedence)
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

                case OperatorOrder::OrderInfix:
                    /* infix evaluation */
                    debugf(EXPR_TEMPLATE_STRING_PREFIX "infix evaluation\n");
                    TopValue = TopStackNode->Val;
                    if (TopValue != nullptr)
                    {
                        BottomValue = TopOperatorNode->Next->Val;

                        /* pop a value, the operator and another value - assume they'll still be there until we're done */
                        HeapPopStack(Parser->pc, nullptr, MEM_ALIGN(sizeof(struct ExpressionStack)) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(TopValue));
                        HeapPopStack(Parser->pc, nullptr, MEM_ALIGN(sizeof(struct ExpressionStack)));
                        HeapPopStack(Parser->pc, BottomValue, MEM_ALIGN(sizeof(struct ExpressionStack)) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(BottomValue));
                        *StackTop = TopOperatorNode->Next->Next;

                        /* do the infix operation */
                        if (Parser->Mode == RunMode::RunModeRun && FoundPrecedence <= *IgnorePrecedence)
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

                case OperatorOrder::OrderNone:
                    /* this should never happen */
                    assert(TopOperatorNode->Order != OperatorOrder::OrderNone);
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
    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionStackCollapse() finished\n");
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
}

/* push an operator on to the expression stack */
void ExpressionStackPushOperator(ParseState *Parser, ExpressionStack **StackTop, OperatorOrder const& Order, LexToken const& Token, int Precedence)
{
    auto *StackNode = static_cast<ExpressionStack *>(VariableAlloc(Parser->pc, Parser, MEM_ALIGN(sizeof(ExpressionStack)), FALSE));
    StackNode->Next = *StackTop;
    StackNode->Order = Order;
    StackNode->Op = Token;
    StackNode->Precedence = Precedence;
    *StackTop = StackNode;
    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionStackPushOperator(%s)\n", tokenToString(Token));
#ifdef FANCY_ERROR_MESSAGES
    StackNode->Line = Parser->Line;
    StackNode->CharacterPos = Parser->CharacterPos;
#endif
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, *StackTop);
#endif
}

/* do the '.' and '->' operators */
void ExpressionGetStructElement(ParseState *Parser, ExpressionStack **StackTop, LexToken const& Token)
{
    Value *Ident;

    /* get the identifier following the '.' or '->' */
    if (nitwit::lex::LexGetToken(Parser, &Ident, true) != TokenIdentifier)
        ProgramFail(Parser, "need an structure or union member after '%s'", (Token == TokenDot) ? "." : "->");

    if (Parser->Mode == RunMode::RunModeRun)
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

        if (StructType->Base != BaseType::TypeStruct && StructType->Base != BaseType::TypeUnion)
            ProgramFail(Parser, "can't use '%s' on something that's not a struct or union %s : it's a %t", (Token == TokenDot) ? "." : "->", (Token == TokenArrow) ? "pointer" : "", ParamVal->Typ);

        if (!nitwit::table::TableGet(StructType->Members, Ident->Val->Identifier, &MemberValue, nullptr, nullptr, nullptr))
            ProgramFail(Parser, "doesn't have a member called '%s'", Ident->Val->Identifier);

        /* pop the value - assume it'll still be there until we're done */
        HeapPopStack(Parser->pc, ParamVal, MEM_ALIGN(sizeof(struct ExpressionStack)) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(StructVal));
        *StackTop = (*StackTop)->Next;

        /* make the result value for this member only */
        Result = VariableAllocValueFromExistingData(Parser, MemberValue->Typ,
                                                    (AnyValue *) (DerefDataLoc + MemberValue->Val->Integer), TRUE,
                                                    (StructVal != nullptr) ? StructVal->LValueFrom : nullptr, nullptr);
        Result->BitField = MemberValue->BitField;                                                                        
        Result->ConstQualifier = MemberValue->ConstQualifier;
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value node from GetStructElement.\n");
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

    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionParse():\n");

    // save the ret function
    const char *RetBeforeName = Parser->ReturnFromFunction;
    Parser->ReturnFromFunction = nullptr;

    do
    {
        ParseState PreState;
        LexToken Token;

        nitwit::parse::ParserCopy(&PreState, Parser);
        Token = nitwit::lex::LexGetToken(Parser, &LexValue, true);
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Looking at token %s.\n", tokenToString(Token));
#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS
        /* if we're debugging, check for a breakpoint */
        if (Parser->DebugMode && Parser->Mode == RunMode::RunModeRun) {
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Performing debug check.\n");
            DebugCheckStatement(Parser, false, 0);
        }
#endif

        if ((( (int)Token > TokenComma && (int)Token <= (int)TokenOpenBracket) ||
               (Token == TokenCloseBracket && BracketPrecedence != 0)) &&
               (Token != TokenColon || TernaryDepth > 0) )
        {
            /* it's an operator with precedence */
            debugf(EXPR_TEMPLATE_STRING_PREFIX "it's an operator with precedence\n");

            if (PrefixState)
            {
                /* expect a prefix operator */
                debugf(EXPR_TEMPLATE_STRING_PREFIX "expecting a prefix operator\n");

                if (OperatorPrecedence[(int)Token].PrefixPrecedence == 0)
                    ProgramFail(Parser, "operator not expected here");

                LocalPrecedence = OperatorPrecedence[(int)Token].PrefixPrecedence;
                Precedence = BracketPrecedence + LocalPrecedence;

                if (Token == TokenOpenBracket)
                {
                    /* it's either a new bracket level or a cast */
                    debugf(EXPR_TEMPLATE_STRING_PREFIX "it's either a new bracket level or a cast\n");

                    LexToken BracketToken = nitwit::lex::LexGetToken(Parser, &LexValue, false);
                    if (IsTypeToken(Parser, BracketToken, LexValue) && (StackTop == nullptr || StackTop->Op != TokenSizeof) )
                    {
                        /* it's a cast - get the new type */
                        debugf(EXPR_TEMPLATE_STRING_PREFIX "it's a cast - get the new type\n");

                        struct ValueType *CastType;
                        char *CastIdentifier;
                        Value *CastTypeValue;

                        int IsConst;
                        TypeParse(Parser, &CastType, &CastIdentifier, nullptr, &IsConst, 0);
                        if (nitwit::lex::LexGetToken(Parser, &LexValue, true) != TokenCloseBracket)
                            ProgramFail(Parser, "brackets not closed 1");

                        /* scan and collapse the stack to the precedence of this infix cast operator, then push */
                        Precedence = BracketPrecedence + OperatorPrecedence[(int)TokenCast].PrefixPrecedence;

                        ExpressionStackCollapse(Parser, &StackTop, Precedence+1, &IgnorePrecedence);
                        CastTypeValue = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->TypeType, FALSE, nullptr, FALSE);
                        CastTypeValue->Val->Typ = CastType;
                        CastTypeValue->ConstQualifier = IsConst;
                        ExpressionStackPushValueNode(Parser, &StackTop, CastTypeValue);
                        ExpressionStackPushOperator(Parser, &StackTop, OperatorOrder::OrderInfix, TokenCast, Precedence);
                    }
                    else
                    {
                        /* boost the bracket operator precedence */
                        debugf(EXPR_TEMPLATE_STRING_PREFIX "boost the bracket operator precedence\n");
                        BracketPrecedence += BRACKET_PRECEDENCE;
                    }
                }
                else
                {
                    /* scan and collapse the stack to the precedence of this operator, then push */
                    debugf(EXPR_TEMPLATE_STRING_PREFIX "scan and collapse the stack to the precedence of this operator, then push\n");

                    /* take some extra care for double prefix operators, e.g. x = - -5, or x = **y */
                    int NextToken = nitwit::lex::LexGetToken(Parser, nullptr, false);
                    int TempPrecedenceBoost = 0;
                    if (NextToken > TokenComma && NextToken < TokenOpenBracket)
                    {
                        int NextPrecedence = OperatorPrecedence[NextToken].PrefixPrecedence;

                        /* two prefix operators with equal precedence? make sure the innermost one runs first */
                        /* FIXME - probably not correct, but can't find a test that fails at this */
                        if (LocalPrecedence == NextPrecedence)
                            TempPrecedenceBoost = -1;
                    }

                    ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                    ExpressionStackPushOperator(Parser, &StackTop, OperatorOrder::OrderPrefix, Token, Precedence + TempPrecedenceBoost);
                }
            }
            else
            {
                /* expect an infix or postfix operator */
                debugf(EXPR_TEMPLATE_STRING_PREFIX "expect an infix or postfix operator\n");

                if (OperatorPrecedence[(int)Token].PostfixPrecedence != 0)
                {
                    debugf(EXPR_TEMPLATE_STRING_PREFIX "PostfixPrecedence != 0\n");
                    Value * TopValue;
                    switch (Token)
                    {
                        case TokenCloseBracket:
                        case TokenRightSquareBracket:
                            if (BracketPrecedence == 0)
                            {
                                /* assume this bracket is after the end of the expression */
                                nitwit::parse::ParserCopy(Parser, &PreState);
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
                            ExpressionStackPushOperator(Parser, &StackTop, OperatorOrder::OrderPostfix, Token, Precedence);
                            ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                            PrefixState = TRUE;
                            TernaryDepth++;
                            TopValue = StackTop->Val;
                            HeapPopStack(Parser->pc, TopValue, MEM_ALIGN(sizeof(struct ExpressionStack)) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(TopValue));
                            StackTop = StackTop->Next;
                            if (!CoerceT<int>(TopValue)){
                                LexToken NextToken = nitwit::lex::LexGetToken(Parser, nullptr, true);
                                int depth = 1;
                                while (depth > 0 && NextToken != TokenEOF){
                                    if (NextToken == TokenQuestionMark) ++depth;
                                    else if (NextToken == TokenColon) --depth;
                                    if (depth > 0)
                                        NextToken = nitwit::lex::LexGetToken(Parser, nullptr, true);
                                }
                            }
                            break;
                        default:
                            /* scan and collapse the stack to the precedence of this operator, then push */
                            Precedence = BracketPrecedence + OperatorPrecedence[(int)Token].PostfixPrecedence;
                            ExpressionStackCollapse(Parser, &StackTop, Precedence, &IgnorePrecedence);
                            ExpressionStackPushOperator(Parser, &StackTop, OperatorOrder::OrderPostfix, Token, Precedence);
                            break;
                    }
                }
                else if (OperatorPrecedence[(int)Token].InfixPrecedence != 0)
                {
                    debugf(EXPR_TEMPLATE_STRING_PREFIX "InfixPrecedence != 0\n");
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
                            int const LHSInt = CoerceT<int>(StackTop->Val);
                            if (((Token == TokenLogicalOr && LHSInt) || (Token == TokenLogicalAnd && !LHSInt))
                                 && (IgnorePrecedence > Precedence) )
                                IgnorePrecedence = Precedence;
                        }

                        /* push the operator on the stack */
                        ExpressionStackPushOperator(Parser, &StackTop, OperatorOrder::OrderInfix, Token, Precedence);
                        PrefixState = TRUE;

                        if (Token == TokenColon) {
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
                    } else if (Token == TokenOpenBracket) {
                        char RunIt = Parser->Mode == RunMode::RunModeRun && Precedence < IgnorePrecedence;
                        if (RunIt)
                            Parser->EnterFunction = StackTop->Next->Val->Val->Identifier;
                        ExpressionParseFunctionCall(Parser, &StackTop,
                            RunIt ? StackTop->Next->Val->Val->Identifier : Parser->pc->StrEmpty, RunIt);
                    }
                } else {
                    ProgramFail(Parser, "operator not expected here");
                }
            }
        }
        else if (Token == TokenIdentifier)
        {
            /* it's a variable, function or a macro */
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Token = Identifier, expecting variable, function or macro\n");
            if (!PrefixState)
                ProgramFail(Parser, "identifier not expected here");

            if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenOpenBracket)
            {
                char RunIt = Parser->Mode == RunMode::RunModeRun && Precedence < IgnorePrecedence;
                char *FuncName = RunIt ? ExpressionResolveFunctionName(Parser, LexValue) : LexValue->Val->Identifier;
                Value * FuncValue;
                if (RunIt){
                    VariableGet(Parser->pc, Parser, FuncName, &FuncValue);
                    if (FuncValue->Val->FuncDef.Intrinsic != nullptr
                            && FuncName != nitwit::table::TableStrRegister(Parser->pc, "__VERIFIER_assume")){
                        Parser->SkipIntrinsic = TRUE;
                    }
                }

#ifndef EXPR_TEMPLATE_VIA_ASSUMPTIONS
                if (Parser->DebugMode && RunIt) {
                    Parser->EnterFunction = FuncName;
                    DebugCheckStatement(Parser, false, 0);
                    Parser->EnterFunction = nullptr;
                }
#endif
                
                nitwit::lex::LexGetToken(Parser, nullptr, true);
                ExpressionParseFunctionCall(Parser, &StackTop, FuncName, RunIt);
#ifndef EXPR_TEMPLATE_VIA_ASSUMPTIONS
                if (Parser->DebugMode && Parser->Mode == RunMode::RunModeRun) {
                    DebugCheckStatement(Parser, false, 0);
                    Parser->ReturnFromFunction = nullptr;
                }
#endif
                
                if (Parser->SkipIntrinsic == TRUE)
                    Parser->SkipIntrinsic = FALSE;
            }
            else
            {
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Its an identifier: '%s'\n", LexValue->Val->Identifier);
                if (Parser->Mode == RunMode::RunModeRun /* && Precedence < IgnorePrecedence */)
                {
                    Value *VariableValue = nullptr;

                    VariableGet(Parser->pc, Parser, LexValue->Val->Identifier, &VariableValue);
                    if (VariableValue->Typ->Base == BaseType::TypeMacro)
                    {
                        /* evaluate a macro as a kind of simple subroutine */
                        struct ParseState MacroParser;
                        Value *MacroResult;

                        nitwit::parse::ParserCopy(&MacroParser, &VariableValue->Val->MacroDef.Body);
                        MacroParser.Mode = Parser->Mode;
                        if (VariableValue->Val->MacroDef.NumParams != 0)
                            ProgramFail(&MacroParser, "macro arguments missing");

                        if (!EXPR_TEMPLATE_PREFIX ExpressionParse(&MacroParser, &MacroResult) || nitwit::lex::LexGetToken(&MacroParser, nullptr, false) != TokenEndOfFunction)
                            ProgramFail(&MacroParser, "expression expected");

                        debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value from InfixOperator (macro).");
                        ExpressionStackPushValueNode(Parser, &StackTop, MacroResult);
                    }
                    else if (VariableValue->Typ->Base == BaseType::TypeVoid)
                        ProgramFail(Parser, "a void value isn't much use here");
                    else if (VariableValue->Typ->Base == BaseType::TypeFunction){ // it's a func ptr identifier
                        Value * FPtr = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->FunctionPtrType, TRUE, FALSE, TRUE);
                        FPtr->Val->Identifier = LexValue->Val->Identifier;
                        ExpressionStackPushValue(Parser, &StackTop, FPtr);
                        VariableFree(Parser->pc, FPtr);
                    }
                    else {
                        /* it's a value variable */
                        VariableValue->VarIdentifier = LexValue->Val->Identifier;
                        ExpressionStackPushLValue(Parser, &StackTop, VariableValue, 0);
                    }
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
            debugf(EXPR_TEMPLATE_STRING_PREFIX "it's a value of some sort, push it\n");

            if (!PrefixState)
                ProgramFail(Parser, "value not expected here");

            PrefixState = FALSE;
            if (IgnorePrecedence <= Precedence) {
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value by type\n");
                ExpressionStackPushValueByType(Parser, &StackTop, &Parser->pc->VoidType);
            }
            else {
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Pushing value.\n");
                ExpressionStackPushValue(Parser, &StackTop, LexValue);
            }
        }
        else if (IsTypeToken(Parser, Token, LexValue))
        {
            /* it's a type. push it on the stack like a value. this is used in sizeof() */
            debugf(EXPR_TEMPLATE_STRING_PREFIX "it's a type. push it on the stack like a value. this is used in sizeof()\n");

            struct ValueType *Typ;
            char *Identifier;
            Value *TypeValue;

            if (!PrefixState)
                ProgramFail(Parser, "type not expected here");

            PrefixState = FALSE;
            nitwit::parse::ParserCopy(Parser, &PreState);
            TypeParse(Parser, &Typ, &Identifier, nullptr, nullptr, 0);
            TypeValue = VariableAllocValueFromType(Parser->pc, Parser, &Parser->pc->TypeType, FALSE, nullptr, FALSE);
            TypeValue->Val->Typ = Typ;
            ExpressionStackPushValueNode(Parser, &StackTop, TypeValue);
        }
        else if ((int)Token == TokenLeftBrace && BracketPrecedence > 0) {
            // sub-block expression with bracket precedence
            debugf(EXPR_TEMPLATE_STRING_PREFIX "it's a sub-block expression with bracket precedence\n");

            struct ParseState OldParser = *(Parser);
            enum LexToken subToken = Token;

            Value *subResult;
            //()
            // parse the complete subblock
            do {
                EXPR_TEMPLATE_PREFIX ExpressionParse(Parser, &(subResult));
                OldParser = *(Parser);
                subToken = nitwit::lex::LexGetToken(Parser, &LexValue, true);
            } while((int)subToken != TokenRightBrace);

            *(Parser) = OldParser;
        }
        else
        {
            /* it isn't a token from an expression */
            debugf(EXPR_TEMPLATE_STRING_PREFIX "it isn't a token from an expression\n");

            nitwit::parse::ParserCopy(Parser, &PreState);
            Done = TRUE;
        }

    } while (!Done);

    /* check that brackets have been closed */
    if (BracketPrecedence > 0)
        ProgramFail(Parser, "brackets not closed 2");

    /* scan and collapse the stack to precedence 0 */
    ExpressionStackCollapse(Parser, &StackTop, 0, &IgnorePrecedence);

    /* fix up the stack and return the result if we're in run mode */
    if (StackTop != nullptr)
    {
        /* all that should be left is a single value on the stack */
        if (Parser->Mode == RunMode::RunModeRun)
        {
            if (StackTop->Order != OperatorOrder::OrderNone || StackTop->Next != nullptr)
                ProgramFail(Parser, "invalid expression");

            *Result = StackTop->Val;
            HeapPopStack(Parser->pc, StackTop, MEM_ALIGN(sizeof(struct ExpressionStack)));
        }
        else
            HeapPopStack(Parser->pc, StackTop->Val, MEM_ALIGN(sizeof(struct ExpressionStack)) + MEM_ALIGN(sizeof(Value)) + TypeStackSizeValue(StackTop->Val));
    }

    Parser->ReturnFromFunction = RetBeforeName;
#ifndef EXPR_TEMPLATE_VIA_ASSUMPTIONS
    /* After parsing an expression, now all of the assignments would be finished -> important for assumptions. */
    if (Parser->DebugMode && Parser->Mode == RunMode::RunModeRun) {
        DebugCheckStatement(Parser, false, 0);
    }
#endif
    Parser->ReturnFromFunction = nullptr;
    Parser->LastConditionBranch = ConditionUndefined;

    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionParse() done\n\n");
#ifdef DEBUG_EXPRESSIONS
    ExpressionStackShow(Parser->pc, StackTop);
#endif
    return StackTop != nullptr;
}


/* do a parameterised macro call */
void ExpressionParseMacroCall(struct ParseState *Parser, struct ExpressionStack **StackTop, const char *MacroName, struct MacroDef *MDef)
{
    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionParseMacroCall()\n");
    Value *ReturnValue = nullptr;
    Value *Param;
    Value **ParamArray = nullptr;
    int ArgCount;
    enum LexToken Token;

    if (Parser->Mode == RunMode::RunModeRun)
    {
        /* create a stack frame for this macro */
        ExpressionStackPushValueByType(Parser, StackTop, &Parser->pc->DoubleType);  /* largest return type there is */

        ReturnValue = (*StackTop)->Val;
        HeapPushStackFrame(Parser->pc);
        ParamArray = static_cast<Value **>(HeapAllocStack(Parser->pc, sizeof(Value *) * MDef->NumParams));
        if (ParamArray == nullptr)
            ProgramFailWithExitCode(Parser, 251, "Out of memory");
    }
    else
        ExpressionPushLongLong(Parser, StackTop, 0);

    /* parse arguments */
    ArgCount = 0;
    do {
        if (EXPR_TEMPLATE_PREFIX ExpressionParse(Parser, &Param))
        {
            if (Parser->Mode == RunMode::RunModeRun)
            {
                if (ArgCount < MDef->NumParams)
                    ParamArray[ArgCount] = Param;
                else
                    ProgramFail(Parser, "too many arguments to %s()", MacroName);
            }

            ArgCount++;
            Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
            if (Token != TokenComma && Token != TokenCloseBracket)
                ProgramFail(Parser, "comma expected");
        }
        else
        {
            /* end of argument list? */
            Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
            if (Token != TokenCloseBracket)
                ProgramFail(Parser, "bad argument");
        }

    } while (Token != TokenCloseBracket);

    if (Parser->Mode == RunMode::RunModeRun)
    {
        /* evaluate the macro */
        struct ParseState MacroParser{};
        int Count;
        Value *EvalValue;

        if (ArgCount < MDef->NumParams)
            ProgramFail(Parser, "not enough arguments to '%s'", MacroName);

        if (MDef->Body.Pos == nullptr)
            ProgramFail(Parser, "'%s' is undefined", MacroName);

        nitwit::parse::ParserCopy(&MacroParser, &MDef->Body);
        MacroParser.Mode = Parser->Mode;
        VariableStackFrameAdd(Parser, MacroName, 0);
        Parser->pc->TopStackFrame->NumParams = ArgCount;
        Parser->pc->TopStackFrame->ReturnValue = ReturnValue;
        for (Count = 0; Count < MDef->NumParams; Count++)
            VariableDefine(Parser->pc, Parser, MDef->ParamName[Count], ParamArray[Count], nullptr, TRUE, false);

        EXPR_TEMPLATE_PREFIX ExpressionParse(&MacroParser, &EvalValue);
        EXPR_TEMPLATE_PREFIX ExpressionAssign(Parser, ReturnValue, EvalValue, TRUE, MacroName, 0, FALSE);
        VariableStackFramePop(Parser);
        HeapPopStackFrame(Parser->pc);
    }
}

/* do a function call */
void ExpressionParseFunctionCall(struct ParseState *Parser, struct ExpressionStack **StackTop, const char *FuncName, int RunIt)
{
    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionParseFunctionCall()\n");
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

        if (FuncValue->Typ->Base == BaseType::TypeMacro)
        {
            /* this is actually a macro, not a function */
            ExpressionParseMacroCall(Parser, StackTop, FuncName, &FuncValue->Val->MacroDef);
            return;
        }

        if (FuncValue->Typ->Base != BaseType::TypeFunction)
            ProgramFail(Parser, "%t is not a function - can't call", FuncValue->Typ);

        ExpressionStackPushValueByType(Parser, StackTop, FuncValue->Val->FuncDef.ReturnType);
        ReturnValue = (*StackTop)->Val;
        HeapPushStackFrame(Parser->pc);
        ParamArray = static_cast<Value **>(HeapAllocStack(Parser->pc,
                                                          sizeof(Value *) * FuncValue->Val->FuncDef.NumParams));
        if (ParamArray == nullptr)
            ProgramFailWithExitCode(Parser, 251, "Out of memory");
    }
    else
    {
        ExpressionPushInt(Parser, StackTop, 0);
//        Parser->Mode = RunModeSkip;
        Parser->Mode = Parser->Mode == RunMode::RunModeGoto ? RunMode::RunModeGoto : RunMode::RunModeSkip;
    }

    /* parse arguments */
    ArgCount = 0;
    do {
        if (RunIt && ArgCount < FuncValue->Val->FuncDef.NumParams)
            ParamArray[ArgCount] = VariableAllocValueFromType(Parser->pc, Parser, FuncValue->Val->FuncDef.ParamType[ArgCount], FALSE, nullptr, FALSE);

        if (EXPR_TEMPLATE_PREFIX ExpressionParse(Parser, &Param))
        {
            if (RunIt)
            {
                if (ArgCount < FuncValue->Val->FuncDef.NumParams)
                {
                    EXPR_TEMPLATE_PREFIX ExpressionAssign(Parser, ParamArray[ArgCount], Param, TRUE, FuncName, ArgCount+1, FALSE);
                    VariableStackPop(Parser, Param);
                }
                else
                {
                    if (!FuncValue->Val->FuncDef.VarArgs)
                        ProgramFail(Parser, "too many arguments to %s()", FuncName);
                }
            }

            ArgCount++;
            Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
            if (Token != TokenComma && Token != TokenCloseBracket)
                ProgramFail(Parser, "comma expected");
        }
        else
        {
            /* end of argument list? */
            Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
            if (Token != TokenCloseBracket)
                ProgramFail(Parser, "bad argument");
        }

    } while (Token != TokenCloseBracket);

    if (RunIt)
    {
        Parser->EnterFunction = nullptr; // remove the enter function identifier before call
        /* run the function */
        if (ArgCount < FuncValue->Val->FuncDef.NumParams)
            ProgramFail(Parser, "not enough arguments to '%s'", FuncName);

#ifndef EXPR_TEMPLATE_VIA_ASSUMPTIONS
		// We are entering a function, this might be a function hooked as the "error" function
		if ((Parser->pc->VerifierErrorFuncName != nullptr) && (strcmp(FuncName, Parser->pc->VerifierErrorFuncName) == 0)) {
			printf("Detected call to marked error function \"%s\"!\n", Parser->pc->VerifierErrorFuncName);
			Parser->pc->VerifierErrorFunctionWasCalled = true;
		}
#endif

        if (FuncValue->Val->FuncDef.Intrinsic == nullptr)
        {
            /* run a user-defined function */
            struct ParseState FuncParser{};
            int Count;
            int OldScopeID = Parser->ScopeID;

            if (FuncValue->Val->FuncDef.Body.Pos == nullptr)
                ProgramFail(Parser, "'%s' is undefined", FuncName);

            nitwit::parse::ParserCopy(&FuncParser, &FuncValue->Val->FuncDef.Body);
            VariableStackFrameAdd(Parser, FuncName, FuncValue->Val->FuncDef.Intrinsic ? FuncValue->Val->FuncDef.NumParams : 0);
            Parser->pc->TopStackFrame->NumParams = ArgCount;
            Parser->pc->TopStackFrame->ReturnValue = ReturnValue;

            /* Function parameters should not go out of scope */
            Parser->ScopeID = -1;

            for (Count = 0; Count < FuncValue->Val->FuncDef.NumParams; Count++) {
                Value * defined = VariableDefine(Parser->pc, Parser, FuncValue->Val->FuncDef.ParamName[Count], ParamArray[Count], nullptr, TRUE, false);
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Defining variable #%i of func '%s': '%s' with type %s.\n", Count, FuncName, FuncValue->Val->FuncDef.ParamName[Count], getType(defined));
                if (ParamArray[Count]->Typ->Base == BaseType::TypeArray){
                    defined->Val = ParamArray[Count]->Val;
                }
            }
            Parser->ScopeID = OldScopeID;

            // track the function
            const char * FunctionBefore = Parser->CurrentFunction;
            Parser->CurrentFunction = FuncName;

            debugf(EXPR_TEMPLATE_STRING_PREFIX "About to parse function '%s'.\n", FuncName);
            if (nitwit::parse::ParseStatement(&FuncParser, TRUE) != ParseResultOk)
                ProgramFail(&FuncParser, "function body expected");

            if (RunIt)
            {
                if (FuncParser.Mode == RunMode::RunModeRun && FuncValue->Val->FuncDef.ReturnType != &Parser->pc->VoidType && strcmp("main", FuncName) != 0) {
					fprintf(stderr, "no value returned from a function returning ");
					PrintType(FuncValue->Val->FuncDef.ReturnType, stderr);
					fprintf(stderr, "\n");
                }

                if (FuncParser.Mode == RunMode::RunModeGoto){
                    do {
                        nitwit::parse::ParserCopyPos(&FuncParser, &FuncValue->Val->FuncDef.Body);
                        FuncParser.FreshGotoSearch = FALSE;
                        nitwit::parse::ParseStatement(&FuncParser, TRUE);
                    } while (FuncParser.Mode == RunMode::RunModeGoto && FuncParser.FreshGotoSearch == TRUE);
                    if (FuncParser.Mode == RunMode::RunModeGoto)
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
    Parser->Mode = OldMode == RunMode::RunModeGoto ? Parser->Mode : OldMode;
}

/* parse an expression */
long long ExpressionParseLongLong(struct ParseState *Parser)
{
    debugf(EXPR_TEMPLATE_STRING_PREFIX "ExpressionParseLongLong()\n");
    Value *Val = nullptr;
    long long Result = 0;

    if (!EXPR_TEMPLATE_PREFIX ExpressionParse(Parser, &Val))
        ProgramFail(Parser, "expression expected");

    if (Parser->Mode == RunMode::RunModeRun)
    {
        if (!IS_NUMERIC_COERCIBLE_PLUS_POINTERS(Val, true))
            ProgramFail(Parser, "integer value expected instead of %t", Val->Typ);

        Result = CoerceT<long long>(Val);
        VariableStackPop(Parser, Val);
    }

    return Result;
}

#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS
    }
#else
    }
#endif
}

#undef EXPR_TEMPLATE_PREFIX
