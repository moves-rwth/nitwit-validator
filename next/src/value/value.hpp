#pragma once

// Run-time values. A value does not know its C type: the evaluator knows the type from the AST and passes the matching
// format to the operations in arithmetic.hpp and to the encoding functions below.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "support/int128.hpp"

namespace nitwit::value {

/// One nondeterministic input of a run, numbered from 0 in the order the inputs were requested.
enum class InputId : std::uint32_t {};

/// The representation of an integer or pointer: width in bits (8, 16, 32, 64 or 128) and signedness. _Bool is
/// {8, false}; that it only holds 0 and 1 is ensured by the conversion to _Bool (C17 6.3.1.2).
struct IntegerFormat {
    std::uint8_t bits = 32;
    bool isSigned = true;

    friend bool operator==(const IntegerFormat&, const IntegerFormat&) = default;
};

enum class FloatFormat : std::uint8_t { Float, Double, LongDouble }; // IEEE binary32, binary64, x87 extended

class Value {
public:
    enum class Kind : std::uint8_t { None, Integer, Float, Double, LongDouble, Bytes };

    /// The value of a void expression.
    Value() = default;

    /// An integer in canonical form: truncated to `format.bits` and sign- or zero-extended to 128 bits.
    [[nodiscard]] static Value integer(UInt128 bits, IntegerFormat format);
    [[nodiscard]] static Value ofFloat(float value) { return Value(Payload(value)); }
    [[nodiscard]] static Value ofDouble(double value) { return Value(Payload(value)); }
    [[nodiscard]] static Value ofLongDouble(long double value) { return Value(Payload(value)); }
    /// The object representation of a struct or union.
    [[nodiscard]] static Value ofBytes(std::vector<std::byte> bytes) { return Value(Payload(std::move(bytes))); }

    [[nodiscard]] Kind kind() const { return static_cast<Kind>(payload_.index()); }

    // Accessors; each requires the matching kind.
    [[nodiscard]] UInt128 bits() const;
    [[nodiscard]] Int128 signedBits() const { return static_cast<Int128>(bits()); }
    [[nodiscard]] float asFloat() const;
    [[nodiscard]] double asDouble() const;
    [[nodiscard]] long double asLongDouble() const;
    [[nodiscard]] std::span<const std::byte> asBytes() const;

    /// The nondeterministic input this value is an unmodified copy of, if any.
    [[nodiscard]] std::optional<InputId> origin() const { return origin_; }
    void setOrigin(std::optional<InputId> origin) { origin_ = origin; }

private:
    // The alternatives are in the order of Kind.
    using Payload = std::variant<std::monostate, UInt128, float, double, long double, std::vector<std::byte>>;

    explicit Value(Payload payload) : payload_(std::move(payload)) {}

    Payload payload_;
    std::optional<InputId> origin_;
};

// Object representations on x86: little-endian; integers take format.bits / 8 bytes. A long double takes the 10 bytes
// of the x87 format; the bytes after them up to out.size() (2 on i386, 6 on x86-64) are padding and written as zeros.

void encodeInteger(const Value& value, IntegerFormat format, std::span<std::byte> out);
[[nodiscard]] Value decodeInteger(std::span<const std::byte> bytes, IntegerFormat format);
void encodeFloat(const Value& value, FloatFormat format, std::span<std::byte> out);
[[nodiscard]] Value decodeFloat(std::span<const std::byte> bytes, FloatFormat format);

} // namespace nitwit::value
