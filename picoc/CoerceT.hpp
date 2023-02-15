
template<typename T>
T CoerceT(Value* Val) {
    T Result;
    switch (Val->Typ->Base) {
    case BaseType::TypeInt:             Result = static_cast<T>(Val->Val->Integer); break;
    case BaseType::TypeChar:            Result = static_cast<T>(Val->Val->Character); break;
    case BaseType::TypeShort:           Result = static_cast<T>(Val->Val->ShortInteger); break;
    case BaseType::TypeLong:            Result = static_cast<T>(Val->Val->LongInteger); break;
    case BaseType::TypeLongLong:        Result = static_cast<T>(Val->Val->LongLongInteger); break;
    case BaseType::TypeUnsignedInt:     Result = static_cast<T>(Val->Val->UnsignedInteger); break;
    case BaseType::TypeUnsignedChar:    Result = static_cast<T>(Val->Val->UnsignedCharacter); break;
    case BaseType::TypeUnsignedShort:   Result = static_cast<T>(Val->Val->UnsignedShortInteger); break;
    case BaseType::TypeUnsignedLong:    Result = static_cast<T>(Val->Val->UnsignedLongInteger); break;
    case BaseType::TypeUnsignedLongLong:Result = static_cast<T>(Val->Val->UnsignedLongLongInteger); break;
    case BaseType::TypePointer:
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            Result = 0.0;
        }
        else {
            Result = (T)(size_t)(Val->Val->Pointer);
        }
        break;        
    case BaseType::TypeDouble:          Result = static_cast<T>(Val->Val->Double); break;
    case BaseType::TypeFloat:           Result = static_cast<T>(Val->Val->Float); break;
    case BaseType::TypeVoid:            Result = 0; break;
    default:                  Result = static_cast<T>(Val->Val->LongLongInteger); break;
    }
    //    int bits = Val->BitField;
    //    if (bits > 0 && !IS_UNSIGNED(Val) && (1 << (bits-1)) & Val->Val->LongLongInteger){
    //        long long mask = (1 << bits) - 1;
    //        Result = -(~(mask & Result) + 1);
    //    }
    return Result;
}
