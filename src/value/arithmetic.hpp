#pragma once

// C arithmetic on the target (C17 6.3 and 6.5, x86 with SSE floating point, i.e. FLT_EVAL_METHOD 0). Operands already
// have the types that the usual arithmetic conversions produce; sema makes those conversions explicit. Operations with
// undefined behaviour report it and still produce the result x86 hardware gives, so that a caller can decide whether
// to stop or to continue like a natively compiled program.
//
// These results model *run-time* code. GCC's constant folding gives other values for the same operations
// ((int)1e10 is INT_MAX, 1 << 32 is 0), so sema must treat any reported undefined behaviour as "not a constant
// expression" (C17 6.6p4) instead of using the value.
//
// Results have no term: the interpreter records the terms of the operations it applies (term.hpp).

#include <cstdint>

#include "support/target.hpp"
#include "value/value.hpp"

namespace nitwit::value {

enum class UndefinedBehavior : std::uint8_t {
    None,
    SignedOverflow,         // + - * / unary- on signed types; also INT_MIN / -1 and INT_MIN % -1
    DivisionByZero,         // integer / or % by zero; the value is 0
    ShiftCountOutOfRange,   // negative count or count >= width (C17 6.5.7p3); x86 masks the count
    ShiftOfNegative,        // left shift of a negative value (C17 6.5.7p4)
    ShiftOverflow,          // left shift of a signed value whose result is not representable (C17 6.5.7p4)
    FloatToIntegerOverflow, // C17 6.3.1.4p1; the value is what GCC-compiled code gives on x86 (see arithmetic.cpp)
};

/// Whether the interpreter ends a run at `kind` (interp/README.md, "Undefined behaviour"). After the other kinds the
/// run continues with the value that comes with them.
[[nodiscard]] constexpr bool endsRun(UndefinedBehavior kind) {
    return kind == UndefinedBehavior::DivisionByZero;
}

struct Result {
    Value value;
    UndefinedBehavior undefinedBehavior = UndefinedBehavior::None;
};

enum class IntegerOperator : std::uint8_t { Add, Sub, Mul, Div, Rem, And, Or, Xor };
enum class ShiftOperator : std::uint8_t { Left, Right };
enum class FloatOperator : std::uint8_t { Add, Sub, Mul, Div };
enum class Comparison : std::uint8_t { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

// ---- Integers (and pointers, as unsigned integers of the pointer width) ----

[[nodiscard]] Result integerBinary(IntegerOperator op, const Value& lhs, const Value& rhs, IntegerFormat format);
/// Both operands are promoted separately (C17 6.5.7p3); the result has the left operand's format. Right shifts of
/// negative values are arithmetic, as in GCC.
[[nodiscard]] Result shift(ShiftOperator op, const Value& lhs, IntegerFormat lhsFormat, const Value& rhs,
                           IntegerFormat rhsFormat);
[[nodiscard]] Result integerNegate(const Value& operand, IntegerFormat format);
[[nodiscard]] Value integerComplement(const Value& operand, IntegerFormat format);
[[nodiscard]] bool integerCompare(Comparison comparison, const Value& lhs, const Value& rhs, IntegerFormat format);

// ---- Floating point (IEEE 754, round to nearest; division by zero gives an infinity, not undefined behaviour) ----

[[nodiscard]] Value floatBinary(FloatOperator op, const Value& lhs, const Value& rhs, FloatFormat format);
[[nodiscard]] Value floatNegate(const Value& operand, FloatFormat format);
/// Comparisons with a NaN are false except NotEqual.
[[nodiscard]] bool floatCompare(Comparison comparison, const Value& lhs, const Value& rhs, FloatFormat format);

// ---- Conversions (C17 6.3.1) ----

/// Integer to integer: truncation to the target width (modulo 2^N, GCC's implementation-defined behaviour for signed
/// targets). Not for _Bool targets; use toBool.
[[nodiscard]] Value convertInteger(const Value& operand, IntegerFormat to);
/// Any scalar to _Bool: 0 if it compares equal to 0, else 1 (C17 6.3.1.2). NaN converts to 1.
[[nodiscard]] Value toBool(const Value& operand);
[[nodiscard]] Value integerToFloat(const Value& operand, IntegerFormat from, FloatFormat to);
/// Truncation toward zero; out of range (including NaN and infinities) is undefined behaviour, whose value depends on
/// the data model. Not for _Bool targets; use toBool.
[[nodiscard]] Result floatToInteger(const Value& operand, FloatFormat from, IntegerFormat to, DataModel model);
[[nodiscard]] Value convertFloat(const Value& operand, FloatFormat from, FloatFormat to);

// ---- Bit-fields (C17 6.7.2.1; GCC allocates them from the least significant bit on x86) ----

/// Reads `width` bits starting at bit `bitOffset` of `storage` (bit 0 is the least significant bit of storage[0]) and
/// returns them as an integer of `format`, sign-extended if the format is signed.
[[nodiscard]] Value readBitField(std::span<const std::byte> storage, unsigned bitOffset, unsigned width,
                                 IntegerFormat format);
/// Writes the low `width` bits of `value` into `storage` at `bitOffset`, leaving the other bits unchanged.
void writeBitField(std::span<std::byte> storage, unsigned bitOffset, unsigned width, const Value& value);

} // namespace nitwit::value
