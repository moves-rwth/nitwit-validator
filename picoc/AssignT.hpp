
/*
template<typename T>
void AssignT_Pure(ParseState* Parser, Value* DestValue, T const& From) {
    std::cerr << "Internal Error: Missing type conversion for AssignT_Pure! Type: " << typeid(T).name() << std::endl;
    throw;
}*/

template<typename T, std::enable_if_t<std::is_floating_point<T>::value, bool> = true>
void AssignT_Pure(ParseState* Parser, Value* DestValue, T const& From) {
    switch (DestValue->Typ->Base) {
    case BaseType::TypeDouble: DestValue->Val->Double = From; break;
    case BaseType::TypeFloat:  DestValue->Val->Float = From; break;
    default:         DestValue->Val->Float = From; break;
    }
}

template<typename T, std::enable_if_t<std::is_integral<T>::value, bool> = true>
void AssignT_Pure(ParseState* Parser, Value* DestValue, T const& From) {
    switch (DestValue->Typ->Base)
    {
    case BaseType::TypeInt:           DestValue->Val->Integer = (int)From; break;
    case BaseType::TypeShort:         DestValue->Val->ShortInteger = (short)From; break;
    case BaseType::TypeChar:          DestValue->Val->Character = (char)From; break;
    case BaseType::TypeLong:          DestValue->Val->LongInteger = (long)From; break;
    case BaseType::TypeLongLong:      DestValue->Val->LongLongInteger = (long long)From; break;
    case BaseType::TypeUnsignedInt:   DestValue->Val->UnsignedInteger = (unsigned int)From; break;
    case BaseType::TypeUnsignedShort: DestValue->Val->UnsignedShortInteger = (unsigned short)From; break;
    case BaseType::TypeUnsignedLong:  DestValue->Val->UnsignedLongInteger = (unsigned long)From; break;
    case BaseType::TypeUnsignedLongLong:  DestValue->Val->UnsignedLongLongInteger = (unsigned long long)From; break;
    case BaseType::TypeUnsignedChar:  DestValue->Val->UnsignedCharacter = (unsigned char)From; break;
    default: break;
    }
    AdjustBitField(Parser, DestValue);
}

#define ASSIGN_T_BOILERPLATE() do { \
    if (!DestValue->IsLValue) { \
        ProgramFail(Parser, "can't assign to this, not an LValue"); \
    }\
    /* check if not const */ \
    if (DestValue->ConstQualifier == TRUE) { \
        ProgramFail(Parser, "can't assign to const %t", DestValue->Typ); \
    } \
} while(false)


template<typename T>
T AssignT(ParseState* Parser, Value* DestValue, T const& From, bool After = false) {
    ASSIGN_T_BOILERPLATE();

    T Result;
    if (After) {
        Result = CoerceT<T>(DestValue);
    }
    else {
        Result = From;
    }

    AssignT_Pure<T>(Parser, DestValue, From);
    return Result;
}
