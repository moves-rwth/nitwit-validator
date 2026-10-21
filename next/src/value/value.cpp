#include "value/value.hpp"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cstring>
#include <limits>
#include <ranges>

#include "support/assert.hpp"

// The value module computes float and double with the host's SSE arithmetic and long double with the host's x87 unit,
// and copies their representations byte for byte: the host must be the target's architecture.
#if !defined(__x86_64__)
#error "NITWIT requires an x86-64 host"
#endif
static_assert(FLT_EVAL_METHOD == 0, "float and double arithmetic must use SSE, without excess precision");
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559);
static_assert(std::numeric_limits<long double>::digits == 64, "long double must be the x87 extended format");
static_assert(std::endian::native == std::endian::little);

namespace nitwit::value {

namespace {

constexpr std::size_t x87Bytes = 10; // 64-bit significand, 15-bit exponent, sign

void checkFormat(IntegerFormat format) {
    NITWIT_ASSERT(format.bits == 8 || format.bits == 16 || format.bits == 32 || format.bits == 64 || format.bits == 128,
                  "integer width must be 8, 16, 32, 64 or 128 bits");
}

template <typename T, typename Payload>
const T& alternative(const Payload& payload) {
    const T* value = std::get_if<T>(&payload);
    NITWIT_ASSERT(value != nullptr, "the value has another kind");
    return *value;
}

} // namespace

Value Value::integer(UInt128 bits, IntegerFormat format) {
    checkFormat(format);
    const unsigned unused = 128u - format.bits;
    const UInt128 shifted = bits << unused;
    // C++20 defines the conversion to Int128 as modular and >> of a negative value as an arithmetic shift.
    const UInt128 extended =
        format.isSigned ? static_cast<UInt128>(static_cast<Int128>(shifted) >> unused) : shifted >> unused;
    return Value(Payload(extended));
}

UInt128 Value::bits() const {
    return alternative<UInt128>(payload_);
}

float Value::asFloat() const {
    return alternative<float>(payload_);
}

double Value::asDouble() const {
    return alternative<double>(payload_);
}

long double Value::asLongDouble() const {
    return alternative<long double>(payload_);
}

std::span<const std::byte> Value::asBytes() const {
    return alternative<std::vector<std::byte>>(payload_);
}

void encodeInteger(const Value& value, IntegerFormat format, std::span<std::byte> out) {
    checkFormat(format);
    NITWIT_ASSERT(out.size() == format.bits / 8u, "integer encoding has the wrong size");
    UInt128 bits = value.bits();
    for (std::byte& byte : out) {
        byte = static_cast<std::byte>(bits & 0xFF);
        bits >>= 8;
    }
}

Value decodeInteger(std::span<const std::byte> bytes, IntegerFormat format) {
    checkFormat(format);
    NITWIT_ASSERT(bytes.size() == format.bits / 8u, "integer encoding has the wrong size");
    UInt128 bits = 0;
    for (const std::byte byte : std::ranges::reverse_view(bytes)) {
        bits = (bits << 8) | std::to_integer<unsigned>(byte);
    }
    return Value::integer(bits, format);
}

void encodeFloat(const Value& value, FloatFormat format, std::span<std::byte> out) {
    switch (format) {
    case FloatFormat::Float: {
        NITWIT_ASSERT(out.size() == sizeof(float), "float encoding has the wrong size");
        const float f = value.asFloat();
        std::memcpy(out.data(), &f, sizeof f);
        return;
    }
    case FloatFormat::Double: {
        NITWIT_ASSERT(out.size() == sizeof(double), "double encoding has the wrong size");
        const double d = value.asDouble();
        std::memcpy(out.data(), &d, sizeof d);
        return;
    }
    case FloatFormat::LongDouble: {
        NITWIT_ASSERT(out.size() >= x87Bytes, "long double encoding is too small");
        const long double l = value.asLongDouble();
        std::memcpy(out.data(), &l, x87Bytes);
        std::ranges::fill(out.subspan(x87Bytes), std::byte{0});
        return;
    }
    }
    NITWIT_UNREACHABLE("unknown float format");
}

Value decodeFloat(std::span<const std::byte> bytes, FloatFormat format) {
    switch (format) {
    case FloatFormat::Float: {
        NITWIT_ASSERT(bytes.size() == sizeof(float), "float encoding has the wrong size");
        float f = 0;
        std::memcpy(&f, bytes.data(), sizeof f);
        return Value::ofFloat(f);
    }
    case FloatFormat::Double: {
        NITWIT_ASSERT(bytes.size() == sizeof(double), "double encoding has the wrong size");
        double d = 0;
        std::memcpy(&d, bytes.data(), sizeof d);
        return Value::ofDouble(d);
    }
    case FloatFormat::LongDouble: {
        NITWIT_ASSERT(bytes.size() >= x87Bytes, "long double encoding is too small");
        long double l = 0; // the host's 6 padding bytes stay zero
        std::memcpy(&l, bytes.data(), x87Bytes);
        return Value::ofLongDouble(l);
    }
    }
    NITWIT_UNREACHABLE("unknown float format");
}

} // namespace nitwit::value
