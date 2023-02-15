#include "BaseType.hpp"

#include <iostream>
#include <typeinfo>

template<typename T> BaseType resolveTypeToBaseType() {
    std::cerr << "Internal Error: Missing type conversion for resolveTypeToBaseType! Type: " << typeid(T).name() << std::endl;
    throw;
}

template<> BaseType resolveTypeToBaseType<float>() { return BaseType::TypeFloat; }
template<> BaseType resolveTypeToBaseType<double>() { return BaseType::TypeDouble; }

template<> BaseType resolveTypeToBaseType<char>() { return BaseType::TypeChar; }
template<> BaseType resolveTypeToBaseType<unsigned char>() { return BaseType::TypeUnsignedChar; }

template<> BaseType resolveTypeToBaseType<short>() { return BaseType::TypeShort; }
template<> BaseType resolveTypeToBaseType<unsigned short>() { return BaseType::TypeUnsignedShort; }

template<> BaseType resolveTypeToBaseType<int>() { return BaseType::TypeInt; }
template<> BaseType resolveTypeToBaseType<unsigned int>() { return BaseType::TypeUnsignedInt; }

template<> BaseType resolveTypeToBaseType<long>() { return BaseType::TypeLong; }
template<> BaseType resolveTypeToBaseType<unsigned long>() { return BaseType::TypeUnsignedLong; }

template<> BaseType resolveTypeToBaseType<long long>() { return BaseType::TypeLongLong; }
template<> BaseType resolveTypeToBaseType<unsigned long long>() { return BaseType::TypeUnsignedLongLong; }
