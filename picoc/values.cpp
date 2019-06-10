#include "interpreter.hpp"

long CoerceInteger(struct Value *Val)
{
    switch (Val->Typ->Base)
    {
        case TypeInt:             return Val->Val->Integer;
        case TypeChar:            return Val->Val->Character;
        case TypeShort:           return Val->Val->ShortInteger;
        case TypeLong:            return Val->Val->LongInteger;
        case TypeLongLong:        return Val->Val->LongLongInteger;
        case TypeUnsignedInt:     return Val->Val->UnsignedInteger;
        case TypeUnsignedChar:    return Val->Val->UnsignedCharacter;
        case TypeUnsignedShort:   return Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:    return Val->Val->UnsignedLongInteger;
        case TypeUnsignedLongLong:return (long long)Val->Val->UnsignedLongLongInteger;
        case TypePointer:         return (long)Val->Val->Pointer;
#ifndef NO_FP
        case TypeDouble:          return (long)Val->Val->Double;
        case TypeFloat:           return (long)Val->Val->Float;
#endif
        default:                  return 0;
    }
}

unsigned long CoerceUnsignedInteger(struct Value *Val)
{
    switch (Val->Typ->Base)
    {
        case TypeInt:             return Val->Val->Integer;
        case TypeChar:            return (long)Val->Val->Character;
        case TypeShort:           return (long)Val->Val->ShortInteger;
        case TypeLong:            return Val->Val->LongInteger;
        case TypeLongLong:        return Val->Val->LongLongInteger;
        case TypeUnsignedInt:     return Val->Val->UnsignedInteger;
        case TypeUnsignedShort:   return Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:    return Val->Val->UnsignedLongInteger;
        case TypeUnsignedChar:    return Val->Val->UnsignedCharacter;
        case TypeUnsignedLongLong:return Val->Val->UnsignedLongLongInteger;
        case TypePointer:         return (unsigned long) Val->Val->Pointer;
#ifndef NO_FP
        case TypeDouble:          return (unsigned long)Val->Val->Double;
        case TypeFloat:           return (unsigned long)Val->Val->Float;

#endif
        default:                  return 0;
    }
}



long long CoerceLongLong(Value *Val)
{
    switch (Val->Typ->Base)
    {
        case TypeInt:             return Val->Val->Integer;
        case TypeChar:            return Val->Val->Character;
        case TypeShort:           return Val->Val->ShortInteger;
        case TypeLong:            return Val->Val->LongInteger;
        case TypeLongLong:        return Val->Val->LongLongInteger;
        case TypeUnsignedInt:     return Val->Val->UnsignedInteger;
        case TypeUnsignedChar:    return Val->Val->UnsignedCharacter;
        case TypeUnsignedShort:   return Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:    return Val->Val->UnsignedLongInteger;
        case TypeUnsignedLongLong:return (long long)Val->Val->UnsignedLongLongInteger;
        case TypePointer:         return (long) Val->Val->Pointer;
#ifndef NO_FP
        case TypeDouble:          return (long long)Val->Val->Double;
        case TypeFloat:           return (long long)Val->Val->Float;
#endif
        default:                  return Val->Val->LongLongInteger;
    }
}

unsigned long long CoerceUnsignedLongLong(Value *Val)
{
    switch (Val->Typ->Base)
    {
        case TypeInt:             return (unsigned long)Val->Val->Integer;
        case TypeChar:            return (unsigned long)Val->Val->Character;
        case TypeShort:           return (unsigned long)Val->Val->ShortInteger;
        case TypeLong:            return (unsigned long)Val->Val->LongInteger;
        case TypeLongLong:        return Val->Val->LongLongInteger;
        case TypeUnsignedInt:     return Val->Val->UnsignedInteger;
        case TypeUnsignedShort:   return Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:    return Val->Val->UnsignedLongInteger;
        case TypeUnsignedLongLong:return Val->Val->UnsignedLongLongInteger;
        case TypeUnsignedChar:    return Val->Val->UnsignedCharacter;
        case TypePointer:         return (unsigned long) Val->Val->Pointer;
#ifndef NO_FP
        case TypeDouble:          return (unsigned long long)Val->Val->Double;
        case TypeFloat:           return (unsigned long long)Val->Val->Float;
#endif
        default:                  return Val->Val->UnsignedLongLongInteger;
    }
}

#ifndef NO_FP
double CoerceDouble(Value *Val)
{
#ifndef BROKEN_FLOAT_CASTS
    int IntVal;
    unsigned UnsignedVal;
    long long LLVal;
    unsigned long long ULLVal;

    switch (Val->Typ->Base)
    {
        case TypeInt:             IntVal = Val->Val->Integer; return (double)IntVal;
        case TypeChar:            IntVal = Val->Val->Character; return (double)IntVal;
        case TypeShort:           IntVal = Val->Val->ShortInteger; return (double)IntVal;
        case TypeLong:            IntVal = Val->Val->LongInteger; return (double)IntVal;
        case TypeLongLong:        LLVal = Val->Val->LongLongInteger; return (double)LLVal;
        case TypeUnsignedInt:     UnsignedVal = Val->Val->UnsignedInteger; return (double)UnsignedVal;
        case TypeUnsignedShort:   UnsignedVal = Val->Val->UnsignedShortInteger; return (double)UnsignedVal;
        case TypeUnsignedLong:    UnsignedVal = Val->Val->UnsignedLongInteger; return (double)UnsignedVal;
        case TypeUnsignedLongLong:ULLVal= Val->Val->UnsignedLongLongInteger; return (double)ULLVal;
        case TypeUnsignedChar:    UnsignedVal = Val->Val->UnsignedCharacter; return (double)UnsignedVal;
        case TypeDouble:          return Val->Val->Double;
        case TypeFloat:           return (double) Val->Val->Float;
        default:                  return 0.0;
    }
#else
    switch (Val->Typ->Base)
    {
        case TypeInt:             return (double)Val->Val->Integer;
        case TypeChar:            return (double)Val->Val->Character;
        case TypeShort:           return (double)Val->Val->ShortInteger;
        case TypeLong:            return (double)Val->Val->LongInteger;
        case TypeUnsignedInt:     return (double)Val->Val->UnsignedInteger;
        case TypeUnsignedShort:   return (double)Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:    return (double)Val->Val->UnsignedLongInteger;
        case TypeUnsignedChar:    return (double)Val->Val->UnsignedCharacter;
        case TypeDouble:              return (double)Val->Val->Double;
        default:                  return 0.0;
    }
#endif
}
float CoerceFloat(Value *Val)
{
#ifndef BROKEN_FLOAT_CASTS
    int IntVal;
    unsigned UnsignedVal;
    long long LLVal;
    unsigned long long ULLVal;

    switch (Val->Typ->Base)
    {
        case TypeInt:             IntVal = Val->Val->Integer; return (float)IntVal;
        case TypeChar:            IntVal = Val->Val->Character; return (float)IntVal;
        case TypeShort:           IntVal = Val->Val->ShortInteger; return (float)IntVal;
        case TypeLong:            IntVal = Val->Val->LongInteger; return (float)IntVal;
        case TypeLongLong:        LLVal = Val->Val->LongLongInteger; return (float)LLVal;
        case TypeUnsignedInt:     UnsignedVal = Val->Val->UnsignedInteger; return (float)UnsignedVal;
        case TypeUnsignedShort:   UnsignedVal = Val->Val->UnsignedShortInteger; return (float)UnsignedVal;
        case TypeUnsignedLong:    UnsignedVal = Val->Val->UnsignedLongInteger; return (float)UnsignedVal;
        case TypeUnsignedLongLong:ULLVal= Val->Val->UnsignedLongLongInteger; return (float)ULLVal;
        case TypeUnsignedChar:    UnsignedVal = Val->Val->UnsignedCharacter; return (float)UnsignedVal;
        case TypeDouble:          return (float) Val->Val->Double;
        case TypeFloat:           return Val->Val->Float;
        default:                  return 0.0;
    }
#else
    switch (Val->Typ->Base)
    {
        case TypeInt:             return (double)Val->Val->Integer;
        case TypeChar:            return (double)Val->Val->Character;
        case TypeShort:           return (double)Val->Val->ShortInteger;
        case TypeLong:            return (double)Val->Val->LongInteger;
        case TypeUnsignedInt:     return (double)Val->Val->UnsignedInteger;
        case TypeUnsignedShort:   return (double)Val->Val->UnsignedShortInteger;
        case TypeUnsignedLong:    return (double)Val->Val->UnsignedLongInteger;
        case TypeUnsignedChar:    return (double)Val->Val->UnsignedCharacter;
        case TypeDouble:              return (double)Val->Val->Double;
        default:                  return 0.0;
    }
#endif
}
#endif

/* assign an integer value */
long AssignInt(struct ParseState *Parser, struct Value *DestValue, long FromInt, int After)
{
    long Result;

    if (!DestValue->IsLValue)
        ProgramFail(Parser, "can't assign to this");

    // check if not const
    if (DestValue->ConstQualifier == TRUE)
        ProgramFail(Parser, "can't assign to const %t", DestValue->Typ);

    if (After)
        Result = CoerceInteger(DestValue);
    else
        Result = FromInt;

    switch (DestValue->Typ->Base)
    {
        case TypeInt:           DestValue->Val->Integer = FromInt; break;
        case TypeShort:         DestValue->Val->ShortInteger = (short)FromInt; break;
        case TypeChar:          DestValue->Val->Character = (char)FromInt; break;
        case TypeLong:          DestValue->Val->LongInteger = (long)FromInt; break;
        case TypeLongLong:      DestValue->Val->LongLongInteger = (long long)FromInt; break;
        case TypeUnsignedInt:   DestValue->Val->UnsignedInteger = (unsigned int)FromInt; break;
        case TypeUnsignedShort: DestValue->Val->UnsignedShortInteger = (unsigned short)FromInt; break;
        case TypeUnsignedLong:  DestValue->Val->UnsignedLongInteger = (unsigned long)FromInt; break;
        case TypeUnsignedChar:  DestValue->Val->UnsignedCharacter = (unsigned char)FromInt; break;
        case TypeUnsignedLongLong:      DestValue->Val->UnsignedLongLongInteger = (unsigned long long)FromInt; break;
        default: break;
    }
    return Result;
}


/* assign an integer value */
long long AssignLongLong(struct ParseState *Parser, Value *DestValue, long long FromInt, int After)
{
    long long Result;

    if (!DestValue->IsLValue)
        ProgramFail(Parser, "can't assign to this");

    // check if not const
    if (DestValue->ConstQualifier == TRUE)
        ProgramFail(Parser, "can't assign to const %t", DestValue->Typ);

    if (After)
        Result = CoerceLongLong(DestValue);
    else
        Result = FromInt;

    switch (DestValue->Typ->Base)
    {
        case TypeInt:           DestValue->Val->Integer = (int) FromInt; break;
        case TypeShort:         DestValue->Val->ShortInteger = (short)FromInt; break;
        case TypeChar:          DestValue->Val->Character = (char)FromInt; break;
        case TypeLong:          DestValue->Val->LongInteger = (long)FromInt; break;
        case TypeLongLong:      DestValue->Val->LongLongInteger = (long long)FromInt; break;
        case TypeUnsignedInt:   DestValue->Val->UnsignedInteger = (unsigned int)FromInt; break;
        case TypeUnsignedShort: DestValue->Val->UnsignedShortInteger = (unsigned short)FromInt; break;
        case TypeUnsignedLong:  DestValue->Val->UnsignedLongInteger = (unsigned long)FromInt; break;
        case TypeUnsignedLongLong:  DestValue->Val->UnsignedLongLongInteger = (unsigned long long)FromInt; break;
        case TypeUnsignedChar:  DestValue->Val->UnsignedCharacter = (unsigned char)FromInt; break;
        default: break;
    }
    return Result;
}

#ifndef NO_FP
/* assign a floating point value */
double AssignFP(struct ParseState *Parser, Value *DestValue, double FromFP)
{
    if (!DestValue->IsLValue)
        ProgramFail(Parser, "can't assign to this");

    // check if not const
    if (DestValue->ConstQualifier == TRUE)
        ProgramFail(Parser, "can't assign to const %t", DestValue->Typ);

    switch (DestValue->Typ->Base) {
        case TypeDouble:
            DestValue->Val->Double = FromFP; break;
        case TypeFloat:
            DestValue->Val->Float = (float) FromFP; break;
        default:
            DestValue->Val->Double = FromFP; break;
    }
    return FromFP;
}
#endif
