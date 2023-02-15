
#include <typeinfo>
#include "BaseType.hpp"
#include "ValueByType.hpp"
#include "CoerceT.hpp"
#include "AssignT.hpp"

#include <type_traits>

#ifdef EXPR_TEMPLATE_VIA_ASSUMPTIONS

template<typename NonDetType, typename DetType>
bool CheckAndResolveVariable(ParseState* Parser, Value* NonDetValue, Value* DetValue, LexToken const& Op) {
    if (TypeIsNonDeterministic(NonDetValue->Typ) != TypeIsNonDeterministic(DetValue->Typ)) {
        /* one of the values is nondet */
        char* Identifier = NonDetValue->VarIdentifier;

        /* nondet resolution */
        NonDetType AssignedValue = CoerceT<NonDetType>(DetValue);

        if (NonDetValue->IsLValue && NonDetValue->LValueFrom != nullptr && NonDetValue->LValueFrom->Typ->Base != BaseType::TypeArray) {
            NonDetValue = NonDetValue->LValueFrom;
        }

        AssignT<NonDetType>(Parser, NonDetValue, AssignedValue);
        switch (Op)
        {
        case TokenAssign:
        case TokenEqual:
            break;
        default:
            ProgramFailWithExitCode(Parser, 247, "Unsupported Operation '%s' for nondet resolution in infix arithmetic on with NonDetType = %s and DetType = %s.", tokenToString(Op), typeid(NonDetType).name(), typeid(DetType).name()); break;
        }

#ifdef VERBOSE
        debugf(EXPR_TEMPLATE_STRING_PREFIX "Resolving NonDet in infix arithmetic with NonDetType = %s and DetType = %s.\n", typeid(NonDetType).name(), typeid(DetType).name());
#endif
        ResolvedVariable(Parser, Identifier, NonDetValue);
        return true;
    }
    return false;
}
#endif

inline bool isAssignmentTypeOp(LexToken Op) {
    return (TokenAssign <= Op && Op <= TokenArithmeticExorAssign);
}

#ifndef EXPR_TEMPLATE_VIA_ASSUMPTIONS
bool PropagateAndResolveNonDeterminism(ParseState* Parser, Value* TopValue, Value* BottomValue, LexToken Op) {
    bool isResultNonDet = true;
    if (Op == TokenAssign) {
        isResultNonDet = TypeIsNonDeterministic(TopValue->Typ);
    }
    else {
        isResultNonDet = TypeIsNonDeterministic(TopValue->Typ) || TypeIsNonDeterministic(BottomValue->Typ);
    }

    if (isResultNonDet) {
        if (isAssignmentTypeOp(Op)) {
            if (BottomValue->IsLValue == TRUE && BottomValue->LValueFrom != nullptr) {
                BottomValue->LValueFrom->Typ = TypeGetNonDeterministic(Parser, BottomValue->LValueFrom->Typ);
#ifdef VERBOSE
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Propagating NonDet to value %s from value %s (LValue).\n", BottomValue->LValueFrom->VarIdentifier, TopValue->VarIdentifier);
#endif
            }
            BottomValue->Typ = TypeGetNonDeterministic(Parser, BottomValue->Typ);
#ifdef VERBOSE
            debugf(EXPR_TEMPLATE_STRING_PREFIX "Propagating NonDet to value %s from value %s.\n", BottomValue->VarIdentifier, TopValue->VarIdentifier);
#endif
        }
    }
    else {
        if (isAssignmentTypeOp(Op)) {
            // We might need to "resolve" array NonDet here
            if (BottomValue->ArrayRoot != nullptr && BottomValue->ArrayIndex != -1) {
                int i = BottomValue->ArrayIndex;
                Value* a = BottomValue->ArrayRoot;
                if (a->Typ->NDListSize > 0) {
                    if (i >= a->Typ->NDListSize) {
                        ProgramFail(Parser, "array index out of bounds (%d >= %d) during NonDet resolution from assignment", i, a->Typ->NDListSize);
                    }
                    setNonDetListElement(a->Typ->NDList, i, false);
#ifdef VERBOSE
                    debugf(EXPR_TEMPLATE_STRING_PREFIX "Resolved NonDet of array entry %i by assignment.\n", i);
#endif
                }
            }
            else {
                if (BottomValue->IsLValue == TRUE && BottomValue->LValueFrom != nullptr) {
                    BottomValue->LValueFrom->Typ = TypeGetDeterministic(Parser, BottomValue->LValueFrom->Typ);
#ifdef VERBOSE
                    debugf(EXPR_TEMPLATE_STRING_PREFIX "Propagating Det to value %s from value %s (LValue).\n", BottomValue->LValueFrom->VarIdentifier, TopValue->VarIdentifier);
#endif
                }
                BottomValue->Typ = TypeGetDeterministic(Parser, BottomValue->Typ);
#ifdef VERBOSE
                debugf(EXPR_TEMPLATE_STRING_PREFIX "Propagating Det to value %s from value %s.\n", BottomValue->VarIdentifier, TopValue->VarIdentifier);
#endif
            }
        }
    }
    return isResultNonDet;
}
#endif