#include "value/arithmetic.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "support/assert.hpp"
#include "support/target.hpp"

namespace nitwit::value {

namespace {

// ---- Integers ----
//
// Integers are computed on their canonical 128-bit form with unsigned host arithmetic, which wraps instead of having
// undefined behaviour. Signed overflow is detected separately.

/// The bits of an integer operand, which must be in the canonical form of `format`: sema made the conversions explicit.
UInt128 operandBits(const Value& operand, IntegerFormat format) {
    const UInt128 bits = operand.bits();
    NITWIT_ASSERT(Value::integer(bits, format).bits() == bits, "integer operand does not have the expected format");
    return bits;
}

/// Whether a canonical signed value is negative; sign extension copies the sign bit to bit 127.
bool isNegative(UInt128 bits) {
    return (bits >> 127) != 0;
}

UInt128 minimum(IntegerFormat format) {
    return format.isSigned ? ~UInt128{0} << (format.bits - 1) : 0;
}

UInt128 magnitude(UInt128 bits) {
    return isNegative(bits) ? UInt128{0} - bits : bits;
}

Result wrapped(UInt128 bits, IntegerFormat format, bool overflows) {
    return {Value::integer(bits, format), overflows ? UndefinedBehavior::SignedOverflow : UndefinedBehavior::None};
}

bool multiplicationOverflows(UInt128 lhs, UInt128 rhs, IntegerFormat format) {
    const bool negative = isNegative(lhs) != isNegative(rhs);
    const UInt128 limit = (UInt128{1} << (format.bits - 1)) - (negative ? 0 : 1);
    const UInt128 a = magnitude(lhs);
    return a != 0 && magnitude(rhs) > limit / a;
}

Result divide(IntegerOperator op, UInt128 lhs, UInt128 rhs, IntegerFormat format) {
    const bool isDivision = op == IntegerOperator::Div;
    if (rhs == 0) {
        return {Value::integer(0, format), UndefinedBehavior::DivisionByZero};
    }
    if (!format.isSigned) {
        return {Value::integer(isDivision ? lhs / rhs : lhs % rhs, format)};
    }
    if (lhs == minimum(format) && rhs == ~UInt128{0}) {
        // idiv traps here on x86 (SIGFPE, also with -fwrapv). The values are those of wrapping arithmetic.
        return {Value::integer(isDivision ? lhs : 0, format), UndefinedBehavior::SignedOverflow};
    }
    // C17 6.5.5p6: the quotient is truncated toward zero, as in C++.
    const auto a = static_cast<Int128>(lhs);
    const auto b = static_cast<Int128>(rhs);
    return {Value::integer(static_cast<UInt128>(isDivision ? a / b : a % b), format)};
}

template <typename T>
bool compare(Comparison comparison, T lhs, T rhs) {
    switch (comparison) {
    case Comparison::Equal:
        return lhs == rhs;
    case Comparison::NotEqual:
        return lhs != rhs;
    case Comparison::Less:
        return lhs < rhs;
    case Comparison::LessEqual:
        return lhs <= rhs;
    case Comparison::Greater:
        return lhs > rhs;
    case Comparison::GreaterEqual:
        return lhs >= rhs;
    }
    NITWIT_UNREACHABLE("unknown comparison");
}

// ---- Floating point ----

template <typename F>
F apply(FloatOperator op, F lhs, F rhs) {
    switch (op) {
    case FloatOperator::Add:
        return lhs + rhs;
    case FloatOperator::Sub:
        return lhs - rhs;
    case FloatOperator::Mul:
        return lhs * rhs;
    case FloatOperator::Div:
        return lhs / rhs;
    }
    NITWIT_UNREACHABLE("unknown floating operator");
}

/// Converts a host arithmetic value to `to` with the host's conversion, which rounds to nearest like the target's.
template <typename T>
Value fromHost(T value, FloatFormat to) {
    switch (to) {
    case FloatFormat::Float:
        return Value::ofFloat(static_cast<float>(value));
    case FloatFormat::Double:
        return Value::ofDouble(static_cast<double>(value));
    case FloatFormat::LongDouble:
        return Value::ofLongDouble(static_cast<long double>(value));
    }
    NITWIT_UNREACHABLE("unknown float format");
}

// ---- Floating to integer, out of range ----
//
// C17 6.3.1.4p1 leaves these conversions undefined. The functions below compute what GCC-compiled code gives on
// x86-64, as confirmed with probes: SSE conversions for float and double, x87 conversions for long double, and libgcc
// routines for 128-bit results.

/// cvttss2si/cvttsd2si (SSE) or fistp in truncation mode (x87) with a `width`-bit destination: the truncated value if
/// it fits, else the "integer indefinite" value 100...0 (Intel SDM).
std::uint64_t convertLikeX86(long double value, unsigned width) {
    const long double truncated = std::trunc(value);
    const long double limit = std::ldexp(1.0L, static_cast<int>(width) - 1);
    if (truncated >= -limit && truncated < limit) {
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(truncated));
    }
    return std::uint64_t{1} << (width - 1);
}

/// GCC's conversion to an unsigned integer of `width` bits when no wider signed conversion is available (64 bits; 32
/// bits on i386): values from 2^(width-1) on are converted after subtracting 2^(width-1) (in the source format), and
/// the top bit is flipped back. NaN takes the path for small values.
template <typename F>
std::uint64_t convertToUnsignedLikeGcc(F value, unsigned width) {
    const F half = std::ldexp(F{1}, static_cast<int>(width) - 1);
    if (value >= half) {
        return convertLikeX86(static_cast<long double>(value - half), width) ^ (std::uint64_t{1} << (width - 1));
    }
    return convertLikeX86(static_cast<long double>(value), width);
}

/// libgcc2.c, __fixunsdfti: converts the high and the low 64 bits separately.
UInt128 fixunsLikeLibgcc(double value) {
    const double two64 = std::ldexp(1.0, 64);
    const std::uint64_t high = convertToUnsignedLikeGcc(value / two64, 64);
    const std::uint64_t low = convertToUnsignedLikeGcc(value - static_cast<double>(high) * two64, 64);
    return (UInt128{high} << 64) | low;
}

/// libgcc2.c, __fixunssfti: computes in double.
UInt128 fixunsLikeLibgcc(float value) {
    return fixunsLikeLibgcc(static_cast<double>(value));
}

/// libgcc2.c, __fixunsxfti: 0 for negative values; the low part is corrected by subtraction.
UInt128 fixunsLikeLibgcc(long double value) {
    if (value < 0) {
        return 0;
    }
    const long double two64 = std::ldexp(1.0L, 64);
    UInt128 result = UInt128{convertToUnsignedLikeGcc(value / two64, 64)} << 64;
    value -= static_cast<long double>(result);
    if (value < 0) {
        result -= convertToUnsignedLikeGcc(-value, 64);
    } else {
        result += convertToUnsignedLikeGcc(value, 64);
    }
    return result;
}

template <typename F>
UInt128 outOfRangeConversion(F value, IntegerFormat to, DataModel model) {
    constexpr bool isX87 = std::is_same_v<F, long double>;
    if (to.bits == 128) {
        // libgcc2.c: __fixsfti and its siblings convert the magnitude with __fixuns*fti.
        return to.isSigned && value < 0 ? UInt128{0} - fixunsLikeLibgcc(-value) : fixunsLikeLibgcc(value);
    }
    if (!to.isSigned && (to.bits == 64 || (to.bits == 32 && model == DataModel::ILP32 && !isX87))) {
        // i386 has no 64-bit SSE conversion for unsigned int (x87 has one, which GCC uses for long double).
        return convertToUnsignedLikeGcc(value, to.bits);
    }
    // Otherwise GCC converts to a signed integer and keeps the low bits: at least 32 bits wide with SSE, at least 16
    // bits with x87, and twice the target's width for unsigned targets.
    constexpr unsigned minimumWidth = isX87 ? 16 : 32;
    const unsigned width = std::max(minimumWidth, to.isSigned ? to.bits : 2u * to.bits);
    return convertLikeX86(static_cast<long double>(value), width);
}

template <typename F>
Result floatToIntegerOf(F value, IntegerFormat to, DataModel model) {
    const long double truncated = std::trunc(static_cast<long double>(value));
    const long double lower = to.isSigned ? -std::ldexp(1.0L, to.bits - 1) : 0.0L;
    const long double upper = std::ldexp(1.0L, to.isSigned ? to.bits - 1 : to.bits);
    if (truncated >= lower && truncated < upper) { // false for NaN
        const UInt128 bits =
            truncated < 0 ? UInt128{0} - static_cast<UInt128>(-truncated) : static_cast<UInt128>(truncated);
        return {Value::integer(bits, to)};
    }
    return {Value::integer(outOfRangeConversion(value, to, model), to), UndefinedBehavior::FloatToIntegerOverflow};
}

void checkBitField(std::size_t storageBytes, unsigned bitOffset, unsigned width) {
    NITWIT_ASSERT(width >= 1 && width <= 128, "bit-field width out of range");
    NITWIT_ASSERT(bitOffset <= storageBytes * 8 && width <= storageBytes * 8 - bitOffset,
                  "bit-field outside its storage");
}

} // namespace

Result integerBinary(IntegerOperator op, const Value& lhs, const Value& rhs, IntegerFormat format) {
    const UInt128 a = operandBits(lhs, format);
    const UInt128 b = operandBits(rhs, format);
    switch (op) {
    case IntegerOperator::Add: {
        const UInt128 sum = Value::integer(a + b, format).bits();
        return wrapped(sum, format,
                       format.isSigned && isNegative(a) == isNegative(b) && isNegative(sum) != isNegative(a));
    }
    case IntegerOperator::Sub: {
        const UInt128 difference = Value::integer(a - b, format).bits();
        return wrapped(difference, format,
                       format.isSigned && isNegative(a) != isNegative(b) && isNegative(difference) != isNegative(a));
    }
    case IntegerOperator::Mul:
        return wrapped(a * b, format, format.isSigned && multiplicationOverflows(a, b, format));
    case IntegerOperator::Div:
    case IntegerOperator::Rem:
        return divide(op, a, b, format);
    case IntegerOperator::And:
        return {Value::integer(a & b, format)};
    case IntegerOperator::Or:
        return {Value::integer(a | b, format)};
    case IntegerOperator::Xor:
        return {Value::integer(a ^ b, format)};
    }
    NITWIT_UNREACHABLE("unknown integer operator");
}

Result shift(ShiftOperator op, const Value& lhs, IntegerFormat lhsFormat, const Value& rhs, IntegerFormat rhsFormat) {
    const UInt128 value = operandBits(lhs, lhsFormat);
    const UInt128 count = operandBits(rhs, rhsFormat);
    const unsigned width = lhsFormat.bits;
    // x86 masks the count to 5 bits for operands of up to 32 bits and to 6 bits for 64-bit operands; GCC's code for
    // 128-bit shifts uses 7 bits.
    const auto masked = static_cast<unsigned>(count & (std::max(width, 32u) - 1));
    UInt128 result = 0;
    if (op == ShiftOperator::Left) {
        result = value << masked;
    } else {
        // C17 6.5.7p5: the right shift of a negative value is implementation-defined; GCC shifts arithmetically.
        result = lhsFormat.isSigned ? static_cast<UInt128>(static_cast<Int128>(value) >> masked) : value >> masked;
    }
    auto undefinedBehavior = UndefinedBehavior::None;
    if (count >= width) { // a negative count, sign-extended, is larger than any width
        undefinedBehavior = UndefinedBehavior::ShiftCountOutOfRange;
    } else if (op == ShiftOperator::Left && lhsFormat.isSigned) {
        if (isNegative(value)) {
            undefinedBehavior = UndefinedBehavior::ShiftOfNegative;
        } else if ((value >> (width - 1 - masked)) != 0) { // a bit would reach the sign bit or beyond
            undefinedBehavior = UndefinedBehavior::ShiftOverflow;
        }
    }
    return {Value::integer(result, lhsFormat), undefinedBehavior};
}

Result integerNegate(const Value& operand, IntegerFormat format) {
    const UInt128 value = operandBits(operand, format);
    return wrapped(UInt128{0} - value, format, format.isSigned && value == minimum(format));
}

Value integerComplement(const Value& operand, IntegerFormat format) {
    return Value::integer(~operandBits(operand, format), format);
}

bool integerCompare(Comparison comparison, const Value& lhs, const Value& rhs, IntegerFormat format) {
    const UInt128 a = operandBits(lhs, format);
    const UInt128 b = operandBits(rhs, format);
    if (format.isSigned) {
        return compare(comparison, static_cast<Int128>(a), static_cast<Int128>(b));
    }
    return compare(comparison, a, b);
}

Value floatBinary(FloatOperator op, const Value& lhs, const Value& rhs, FloatFormat format) {
    switch (format) {
    case FloatFormat::Float:
        return Value::ofFloat(apply(op, lhs.asFloat(), rhs.asFloat()));
    case FloatFormat::Double:
        return Value::ofDouble(apply(op, lhs.asDouble(), rhs.asDouble()));
    case FloatFormat::LongDouble:
        return Value::ofLongDouble(apply(op, lhs.asLongDouble(), rhs.asLongDouble()));
    }
    NITWIT_UNREACHABLE("unknown float format");
}

Value floatNegate(const Value& operand, FloatFormat format) {
    switch (format) {
    case FloatFormat::Float:
        return Value::ofFloat(-operand.asFloat());
    case FloatFormat::Double:
        return Value::ofDouble(-operand.asDouble());
    case FloatFormat::LongDouble:
        return Value::ofLongDouble(-operand.asLongDouble());
    }
    NITWIT_UNREACHABLE("unknown float format");
}

bool floatCompare(Comparison comparison, const Value& lhs, const Value& rhs, FloatFormat format) {
    switch (format) {
    case FloatFormat::Float:
        return compare(comparison, lhs.asFloat(), rhs.asFloat());
    case FloatFormat::Double:
        return compare(comparison, lhs.asDouble(), rhs.asDouble());
    case FloatFormat::LongDouble:
        return compare(comparison, lhs.asLongDouble(), rhs.asLongDouble());
    }
    NITWIT_UNREACHABLE("unknown float format");
}

Value convertInteger(const Value& operand, IntegerFormat to) {
    // The canonical form is already sign- or zero-extended according to the source format (C17 6.3.1.3).
    return Value::integer(operand.bits(), to);
}

Value toBool(const Value& operand) {
    bool isTrue = false;
    switch (operand.kind()) {
    case Value::Kind::Integer:
        isTrue = operand.bits() != 0;
        break;
    case Value::Kind::Float:
        isTrue = operand.asFloat() != 0.0F;
        break;
    case Value::Kind::Double:
        isTrue = operand.asDouble() != 0.0;
        break;
    case Value::Kind::LongDouble:
        isTrue = operand.asLongDouble() != 0.0L;
        break;
    case Value::Kind::None:
    case Value::Kind::Bytes:
        NITWIT_UNREACHABLE("conversion of a non-scalar to _Bool");
    }
    return Value::integer(isTrue ? 1u : 0u, IntegerFormat{.bits = 8, .isSigned = false});
}

Value integerToFloat(const Value& operand, IntegerFormat from, FloatFormat to) {
    const UInt128 bits = operandBits(operand, from);
    // The host converts with a single rounding, like the target; 64-bit host types keep the common case off libgcc.
    if (from.bits <= 64) {
        return from.isSigned ? fromHost(static_cast<std::int64_t>(static_cast<Int128>(bits)), to)
                             : fromHost(static_cast<std::uint64_t>(bits), to);
    }
    return from.isSigned ? fromHost(static_cast<Int128>(bits), to) : fromHost(bits, to);
}

Result floatToInteger(const Value& operand, FloatFormat from, IntegerFormat to, DataModel model) {
    switch (from) {
    case FloatFormat::Float:
        return floatToIntegerOf(operand.asFloat(), to, model);
    case FloatFormat::Double:
        return floatToIntegerOf(operand.asDouble(), to, model);
    case FloatFormat::LongDouble:
        return floatToIntegerOf(operand.asLongDouble(), to, model);
    }
    NITWIT_UNREACHABLE("unknown float format");
}

Value convertFloat(const Value& operand, FloatFormat from, FloatFormat to) {
    switch (from) {
    case FloatFormat::Float:
        return fromHost(operand.asFloat(), to);
    case FloatFormat::Double:
        return fromHost(operand.asDouble(), to);
    case FloatFormat::LongDouble:
        return fromHost(operand.asLongDouble(), to);
    }
    NITWIT_UNREACHABLE("unknown float format");
}

Value readBitField(std::span<const std::byte> storage, unsigned bitOffset, unsigned width, IntegerFormat format) {
    checkBitField(storage.size(), bitOffset, width);
    NITWIT_ASSERT(width <= format.bits, "bit-field wider than its type");
    UInt128 bits = 0;
    for (unsigned done = 0; done < width;) {
        const unsigned bit = bitOffset + done;
        const unsigned shift = bit % 8;
        const unsigned count = std::min(8 - shift, width - done);
        const unsigned piece = (std::to_integer<unsigned>(storage[bit / 8]) >> shift) & ((1u << count) - 1);
        bits |= UInt128{piece} << done;
        done += count;
    }
    if (format.isSigned && width < 128) {
        // Sign-extend from the field's width.
        bits = static_cast<UInt128>(static_cast<Int128>(bits << (128 - width)) >> (128 - width));
    }
    return Value::integer(bits, format);
}

void writeBitField(std::span<std::byte> storage, unsigned bitOffset, unsigned width, const Value& value) {
    checkBitField(storage.size(), bitOffset, width);
    const UInt128 bits = value.bits();
    for (unsigned done = 0; done < width;) {
        const unsigned bit = bitOffset + done;
        const unsigned shift = bit % 8;
        const unsigned count = std::min(8 - shift, width - done);
        const unsigned mask = ((1u << count) - 1) << shift;
        const auto piece = static_cast<unsigned>(bits >> done) << shift;
        std::byte& byte = storage[bit / 8];
        byte = static_cast<std::byte>((std::to_integer<unsigned>(byte) & ~mask) | (piece & mask));
        done += count;
    }
}

} // namespace nitwit::value
