#include "support/target.hpp"

#include "support/assert.hpp"

namespace nitwit {

ScalarLayout TargetInfo::layout(ScalarType type) const {
    const bool lp64 = model_ == DataModel::LP64;
    switch (type) {
    case ScalarType::Bool:
    case ScalarType::Char:
    case ScalarType::SignedChar:
    case ScalarType::UnsignedChar:
        return {1, 1, 1};
    case ScalarType::Short:
    case ScalarType::UnsignedShort:
        return {2, 2, 2};
    case ScalarType::Int:
    case ScalarType::UnsignedInt:
    case ScalarType::Float:
        return {4, 4, 4};
    case ScalarType::Long:
    case ScalarType::UnsignedLong:
    case ScalarType::Pointer:
        return lp64 ? ScalarLayout{8, 8, 8} : ScalarLayout{4, 4, 4};
    // The i386 System V ABI aligns 8-byte scalars to 4 bytes inside records; GCC still prefers 8 elsewhere.
    case ScalarType::LongLong:
    case ScalarType::UnsignedLongLong:
    case ScalarType::Double:
        return lp64 ? ScalarLayout{8, 8, 8} : ScalarLayout{8, 4, 8};
    case ScalarType::Int128:
    case ScalarType::UnsignedInt128:
        return {16, 16, 16};
    // x87 extended precision: 10 significant bytes, padded to 12 (i386) or 16 (x86-64).
    case ScalarType::LongDouble:
        return lp64 ? ScalarLayout{16, 16, 16} : ScalarLayout{12, 4, 4};
    }
    NITWIT_UNREACHABLE("unknown scalar type");
}

bool TargetInfo::isSigned(ScalarType type) const {
    switch (type) {
    case ScalarType::Char:
    case ScalarType::SignedChar:
    case ScalarType::Short:
    case ScalarType::Int:
    case ScalarType::Long:
    case ScalarType::LongLong:
    case ScalarType::Int128:
        return true;
    default:
        return false;
    }
}

ScalarType TargetInfo::sizeType() const {
    return model_ == DataModel::LP64 ? ScalarType::UnsignedLong : ScalarType::UnsignedInt;
}

ScalarType TargetInfo::ptrdiffType() const {
    return model_ == DataModel::LP64 ? ScalarType::Long : ScalarType::Int;
}

} // namespace nitwit
