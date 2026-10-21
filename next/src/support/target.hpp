#pragma once

// The targets NITWIT interprets programs for: Linux with the System V ABI on i386 (ILP32) or x86-64 (LP64), the two
// data models of SV-COMP.

#include <cstdint>

namespace nitwit {

enum class DataModel : std::uint8_t { ILP32, LP64 };

/// The C scalar types whose representation depends on the target.
enum class ScalarType : std::uint8_t {
    Bool,
    Char,
    SignedChar,
    UnsignedChar,
    Short,
    UnsignedShort,
    Int,
    UnsignedInt,
    Long,
    UnsignedLong,
    LongLong,
    UnsignedLongLong,
    Int128,
    UnsignedInt128,
    Float,
    Double,
    LongDouble,
    Pointer,
};

struct ScalarLayout {
    std::uint8_t size = 0;           // sizeof, in bytes
    std::uint8_t align = 0;          // _Alignof and the alignment as a record member (ABI alignment)
    std::uint8_t preferredAlign = 0; // GCC's __alignof__; differs from align on i386, e.g. 8 for double
};

class TargetInfo {
public:
    static TargetInfo forDataModel(DataModel model) { return TargetInfo(model); }

    [[nodiscard]] DataModel dataModel() const { return model_; }
    [[nodiscard]] ScalarLayout layout(ScalarType type) const;
    [[nodiscard]] unsigned pointerBits() const { return model_ == DataModel::ILP32 ? 32 : 64; }

    /// Whether an integer type is signed; plain `char` is signed on x86. False for floating and pointer types.
    [[nodiscard]] bool isSigned(ScalarType type) const;

    [[nodiscard]] ScalarType sizeType() const; // size_t
    [[nodiscard]] ScalarType ptrdiffType() const;
    [[nodiscard]] ScalarType wcharType() const { return ScalarType::Int; }

    /// GCC accepts __int128 only for 64-bit targets.
    [[nodiscard]] bool hasInt128() const { return model_ == DataModel::LP64; }

    /// The alignment malloc guarantees, alignof(max_align_t).
    [[nodiscard]] std::uint8_t maxAlign() const { return 16; }

private:
    explicit TargetInfo(DataModel model) : model_(model) {}

    DataModel model_;
};

} // namespace nitwit
