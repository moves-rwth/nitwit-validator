#include "verbose.hpp"

#include "CoerceT.hpp"

#ifndef VERBOSE
void cw_verbose(std::string const& Format, ...) {}
#endif

void printTable(struct Table* Tbl) {
    struct TableEntry* Entry;
    for (short i = 0; i < Tbl->Size; ++i) {
        for (Entry = Tbl->HashTable[i]; Entry != nullptr; Entry = Entry->Next)
        {
            if (IS_FP(Entry->p.v.Val)) {
                double fp = CoerceT<double>(Entry->p.v.Val);
                printf("Variable '%s' has value %2.20f (NonDet: %s).\n", Entry->p.v.Key, fp, TypeIsNonDeterministic(Entry->p.v.Val->Typ) ? "y" : "n");
            }
            else {
                int i = CoerceT<long long>(Entry->p.v.Val);
                printf("Variable '%s' has value %d (NonDet: %s).\n", Entry->p.v.Key, i, TypeIsNonDeterministic(Entry->p.v.Val->Typ) ? "y" : "n");
            }
        }
    }    
}

void printVarTable(Picoc* pc) {
    printf("Global Table:\n");
    printTable(&pc->GlobalTable);
    if (pc->TopStackFrame != nullptr) {
        printf("Local Table:\n");
        printTable(&pc->TopStackFrame->LocalTable);
    }
}

char const* tokenToString(LexToken token) {
    switch (token) {
    case TokenNone: return "TokenNone";
    case TokenComma: return "TokenComma";
    case TokenAssign: return "TokenAssign";
    case TokenAddAssign: return "TokenAddAssign";
    case TokenSubtractAssign: return "TokenSubtractAssign";
    case TokenMultiplyAssign: return "TokenMultiplyAssign";
    case TokenDivideAssign: return "TokenDivideAssign";
    case TokenModulusAssign: return "TokenModulusAssign";
    case TokenShiftLeftAssign: return "TokenShiftLeftAssign";
    case TokenShiftRightAssign: return "TokenShiftRightAssign";
    case TokenArithmeticAndAssign: return "TokenArithmeticAndAssign";
    case TokenArithmeticOrAssign: return "TokenArithmeticOrAssign";
    case TokenArithmeticExorAssign: return "TokenArithmeticExorAssign";
    case TokenQuestionMark: return "TokenQuestionMark";
    case TokenColon: return "TokenColon";
    case TokenLogicalOr: return "TokenLogicalOr";
    case TokenLogicalAnd: return "TokenLogicalAnd";
    case TokenArithmeticOr: return "TokenArithmeticOr";
    case TokenArithmeticExor: return "TokenArithmeticExor";
    case TokenAmpersand: return "TokenAmpersand";
    case TokenEqual: return "TokenEqual";
    case TokenNotEqual: return "TokenNotEqual";
    case TokenLessThan: return "TokenLessThan";
    case TokenGreaterThan: return "TokenGreaterThan";
    case TokenLessEqual: return "TokenLessEqual";
    case TokenGreaterEqual: return "TokenGreaterEqual";
    case TokenShiftLeft: return "TokenShiftLeft";
    case TokenShiftRight: return "TokenShiftRight";
    case TokenPlus: return "TokenPlus";
    case TokenMinus: return "TokenMinus";
    case TokenAsterisk: return "TokenAsterisk";
    case TokenSlash: return "TokenSlash";
    case TokenModulus: return "TokenModulus";
    case TokenIncrement: return "TokenIncrement";
    case TokenDecrement: return "TokenDecrement";
    case TokenUnaryNot: return "TokenUnaryNot";
    case TokenUnaryExor: return "TokenUnaryExor";
    case TokenSizeof: return "TokenSizeof";
    case TokenCast: return "TokenCast";
    case TokenLeftSquareBracket: return "TokenLeftSquareBracket";
    case TokenRightSquareBracket: return "TokenRightSquareBracket";
    case TokenDot: return "TokenDot";
    case TokenArrow: return "TokenArrow";
    case TokenOpenBracket: return "TokenOpenBracket";
    case TokenCloseBracket: return "TokenCloseBracket";
    case TokenIdentifier: return "TokenIdentifier";
    case TokenIntegerConstant: return "TokenIntegerConstant";
    case TokenUnsignedIntConstanst: return "TokenUnsignedIntConstanst";
    case TokenLLConstanst: return "TokenLLConstanst";
    case TokenUnsignedLLConstanst: return "TokenUnsignedLLConstanst";
    case TokenFloatConstant: return "TokenFloatConstant";
    case TokenDoubleConstant: return "TokenDoubleConstant";
    case TokenStringConstant: return "TokenStringConstant";
    case TokenCharacterConstant: return "TokenCharacterConstant";
    case TokenSemicolon: return "TokenSemicolon";
    case TokenEllipsis: return "TokenEllipsis";
    case TokenLeftBrace: return "TokenLeftBrace";
    case TokenRightBrace: return "TokenRightBrace";
    case TokenIntType: return "TokenIntType";
    case TokenCharType: return "TokenCharType";
    case TokenFloatType: return "TokenFloatType";
    case TokenDoubleType: return "TokenDoubleType";
    case TokenVoidType: return "TokenVoidType";
    case TokenEnumType: return "TokenEnumType";
    case TokenConst: return "TokenConst";
    case TokenLongType: return "TokenLongType";
    case TokenSignedType: return "TokenSignedType";
    case TokenShortType: return "TokenShortType";
    case TokenStaticType: return "TokenStaticType";
    case TokenAutoType: return "TokenAutoType";
    case TokenRegisterType: return "TokenRegisterType";
    case TokenExternType: return "TokenExternType";
    case TokenStructType: return "TokenStructType";
    case TokenUnionType: return "TokenUnionType";
    case TokenUnsignedType: return "TokenUnsignedType";
    case TokenTypedef: return "TokenTypedef";
    case TokenContinue: return "TokenContinue";
    case TokenDo: return "TokenDo";
    case TokenElse: return "TokenElse";
    case TokenFor: return "TokenFor";
    case TokenGoto: return "TokenGoto";
    case TokenIf: return "TokenIf";
    case TokenWhile: return "TokenWhile";
    case TokenBreak: return "TokenBreak";
    case TokenSwitch: return "TokenSwitch";
    case TokenCase: return "TokenCase";
    case TokenDefault: return "TokenDefault";
    case TokenReturn: return "TokenReturn";
    case TokenHashDefine: return "TokenHashDefine";
    case TokenHashInclude: return "TokenHashInclude";
    case TokenHashIf: return "TokenHashIf";
    case TokenHashIfdef: return "TokenHashIfdef";
    case TokenHashIfndef: return "TokenHashIfndef";
    case TokenHashElse: return "TokenHashElse";
    case TokenHashEndif: return "TokenHashEndif";
    case TokenNew: return "TokenNew";
    case TokenDelete: return "TokenDelete";
    case TokenOpenMacroBracket: return "TokenOpenMacroBracket";
    case TokenAttribute: return "TokenAttribute";
    case TokenNoReturn: return "TokenNoReturn";
    case TokenIgnore: return "TokenIgnore";
    case TokenPragma: return "TokenPragma";
    case TokenWitnessResult: return "TokenWitnessResult";
    case TokenEOF: return "TokenEOF";
    case TokenEndOfLine: return "TokenEndOfLine";
    case TokenEndOfFunction: return "TokenEndOfFunction";
    default: return "UNKNOWN_INVALID";
    }
}

char const* getType(Value* value) {
    return getType(value->Typ);
}

char const* getType(ValueType* Typ) {
    switch (Typ->Base) {
    case BaseType::TypeVoid: return "BaseType::TypeVoid";
    case BaseType::TypeInt: return "BaseType::TypeInt";
    case BaseType::TypeShort: return "BaseType::TypeShort";
    case BaseType::TypeChar: return "BaseType::TypeChar";
    case BaseType::TypeLong: return "BaseType::TypeLong";
    case BaseType::TypeLongLong: return "BaseType::TypeLongLong";
    case BaseType::TypeUnsignedInt: return "BaseType::TypeUnsignedInt";
    case BaseType::TypeUnsignedShort: return "BaseType::TypeUnsignedShort";
    case BaseType::TypeUnsignedChar: return "BaseType::TypeUnsignedChar";
    case BaseType::TypeUnsignedLong: return "BaseType::TypeUnsignedLong";
    case BaseType::TypeUnsignedLongLong: return "BaseType::TypeUnsignedLongLong";
    case BaseType::TypeDouble: return "BaseType::TypeDouble";
    case BaseType::TypeFloat: return "BaseType::TypeFloat";
    case BaseType::TypeFunction: return "BaseType::TypeFunction";
    case BaseType::TypeFunctionPtr: return "BaseType::TypeFunctionPtr";
    case BaseType::TypeMacro: return "BaseType::TypeMacro";
    case BaseType::TypePointer: return "BaseType::TypePointer";
    case BaseType::TypeArray: return "BaseType::TypeArray";
    case BaseType::TypeStruct: return "BaseType::TypeStruct";
    case BaseType::TypeUnion: return "BaseType::TypeUnion";
    case BaseType::TypeEnum: return "BaseType::TypeEnum";
    case BaseType::TypeGotoLabel: return "BaseType::TypeGotoLabel";
    case BaseType::Type_Type: return "BaseType::Type_Type";
    default: return "UNKNOWN_INVALID";
    }
}