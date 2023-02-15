
template<typename T>
T getValueByType(Value* value) {
    std::cerr << "Internal Error: Missing type conversion for getValueByType!" << std::endl;
    throw;
}

template<> float getValueByType<float>(Value* value) { if (value->Typ->Base != BaseType::TypeFloat) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'float'!" << std::endl; throw; } return value->Val->Float; }
template<> double getValueByType<double>(Value* value) { if (value->Typ->Base != BaseType::TypeDouble) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'double'!" << std::endl; throw; } return value->Val->Double; }

template<> char getValueByType<char>(Value* value) { if (value->Typ->Base != BaseType::TypeChar) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'char'!" << std::endl; throw; } return value->Val->Character; }
template<> unsigned char getValueByType<unsigned char>(Value* value) { if (value->Typ->Base != BaseType::TypeUnsignedChar) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'unsigned char'!" << std::endl; throw; } return value->Val->UnsignedCharacter; }

template<> short getValueByType<short>(Value* value) { if (value->Typ->Base != BaseType::TypeShort) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'short'!" << std::endl; throw; } return value->Val->ShortInteger; }
template<> unsigned short getValueByType<unsigned short>(Value* value) { if (value->Typ->Base != BaseType::TypeUnsignedShort) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'unsigned short'!" << std::endl; throw; } return value->Val->UnsignedShortInteger; }

template<> int getValueByType<int>(Value* value) { if (value->Typ->Base != BaseType::TypeInt) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'int'!" << std::endl; throw; } return value->Val->Integer; }
template<> unsigned int getValueByType<unsigned int>(Value* value) { if (value->Typ->Base != BaseType::TypeUnsignedInt) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'unsigned int'!" << std::endl; throw; } return value->Val->UnsignedInteger; }

template<> long getValueByType<long>(Value* value) { if (value->Typ->Base != BaseType::TypeLong) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'long'!" << std::endl; throw; } return value->Val->LongInteger; }
template<> unsigned long getValueByType<unsigned long>(Value* value) { if (value->Typ->Base != BaseType::TypeUnsignedLong) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'unsigned long'!" << std::endl; throw; } return value->Val->UnsignedLongInteger; }

template<> long long getValueByType<long long>(Value* value) { if (value->Typ->Base != BaseType::TypeLongLong) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'long long'!" << std::endl; throw; } return value->Val->LongLongInteger; }
template<> unsigned long long getValueByType<unsigned long long>(Value* value) { if (value->Typ->Base != BaseType::TypeUnsignedLongLong) { std::cerr << "Invalid conversion, variable has type '" << getType(value) << "', requested was 'unsigned long long'!" << std::endl; throw; } return value->Val->UnsignedLongLongInteger; }

inline bool valueIsFloat(Value* value) {
    return value->Typ->Base == BaseType::TypeFloat;
}

inline bool valueIsDouble(Value* value) {
    return value->Typ->Base == BaseType::TypeDouble;
}

inline bool valueIsFloatOrDouble(Value* value) {
    return valueIsFloat(value) || valueIsDouble(value);
}

template<typename T>
inline ValueType* typeByT(ParseState* Parser, bool ndType) {
    if constexpr (std::is_same_v<T, float>) {
        return (ndType) ? &Parser->pc->FloatNDType : &Parser->pc->FloatType;
    }
    else if constexpr (std::is_same_v<T, double>) {
        return (ndType) ? &Parser->pc->DoubleNDType : &Parser->pc->DoubleType;
    }
    else if constexpr (std::is_same_v<T, char>) {
        return (ndType) ? &Parser->pc->CharNDType : &Parser->pc->CharType;
    }
    else if constexpr (std::is_same_v<T, unsigned char>) {
        return (ndType) ? &Parser->pc->UnsignedCharNDType : &Parser->pc->UnsignedCharType;
    }
    else if constexpr (std::is_same_v<T, short>) {
        return (ndType) ? &Parser->pc->ShortNDType : &Parser->pc->ShortType;
    }
    else if constexpr (std::is_same_v<T, unsigned short>) {
        return (ndType) ? &Parser->pc->UnsignedShortNDType : &Parser->pc->UnsignedShortType;
    }
    else if constexpr (std::is_same_v<T, int>) {
        return (ndType) ? &Parser->pc->IntNDType : &Parser->pc->IntType;
    }
    else if constexpr (std::is_same_v<T, unsigned int>) {
        return (ndType) ? &Parser->pc->UnsignedIntNDType : &Parser->pc->UnsignedIntType;
    }
    else if constexpr (std::is_same_v<T, long>) {
        return (ndType) ? &Parser->pc->LongNDType : &Parser->pc->LongType;
    }
    else if constexpr (std::is_same_v<T, unsigned long>) {
        return (ndType) ? &Parser->pc->UnsignedLongNDType : &Parser->pc->UnsignedLongType;
    }
    else if constexpr (std::is_same_v<T, long long>) {
        return (ndType) ? &Parser->pc->LongLongNDType : &Parser->pc->LongLongType;
    }
    else if constexpr (std::is_same_v<T, unsigned long long>) {
        return (ndType) ? &Parser->pc->UnsignedLongLongNDType : &Parser->pc->UnsignedLongLongType;
    }
    else {
        std::cerr << "Internal Error: Missing type conversion for typeByT! Type: " << typeid(T).name() << std::endl;
        throw;
    }
}
