#include "validate/values.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>

#include "ast/casting.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::validate {

namespace {

bool isBool(ast::AstContext& context, ast::QualType type) {
    const auto* builtin = ast::dyn_cast<ast::BuiltinType>(context.canonical(type).type());
    return builtin != nullptr && builtin->builtinKind() == ast::BuiltinKind::Bool;
}

// The smallest and largest value of an integer format.
Int128 minimumOf(value::IntegerFormat format) {
    if (!format.isSigned) {
        return 0;
    }
    return format.bits == 128 ? std::numeric_limits<Int128>::min() : -(Int128{1} << (format.bits - 1));
}

UInt128 maximumOf(value::IntegerFormat format) {
    const unsigned bits = format.isSigned ? format.bits - 1u : format.bits;
    return bits == 128 ? std::numeric_limits<UInt128>::max() : (UInt128{1} << bits) - 1;
}

std::optional<value::Value> satisfyingInteger(value::Comparison op, const value::Value& constant,
                                              value::IntegerFormat format) {
    // In a signed format, the canonical bits are the sign-extended value; in an unsigned one, the zero-extended value.
    const bool isMinimum = format.isSigned ? constant.signedBits() == minimumOf(format) : constant.bits() == 0;
    const bool isMaximum = format.isSigned ? constant.signedBits() >= 0 && constant.bits() == maximumOf(format)
                                           : constant.bits() == maximumOf(format);
    switch (op) {
    case value::Comparison::Equal:
    case value::Comparison::LessEqual:
    case value::Comparison::GreaterEqual:
        return constant;
    case value::Comparison::NotEqual:
        return value::Value::integer(constant.bits() == 0 ? 1 : 0, format);
    case value::Comparison::Less:
        return isMinimum ? std::nullopt : std::optional(value::Value::integer(constant.bits() - 1, format));
    case value::Comparison::Greater:
        return isMaximum ? std::nullopt : std::optional(value::Value::integer(constant.bits() + 1, format));
    }
    std::unreachable();
}

template <typename T>
std::optional<T> satisfyingFloating(value::Comparison op, T constant) {
    if (std::isnan(constant)) {
        return op == value::Comparison::NotEqual ? std::optional<T>(0) : std::nullopt; // only != holds for NaN
    }
    constexpr T infinity = std::numeric_limits<T>::infinity();
    switch (op) {
    case value::Comparison::Equal:
    case value::Comparison::LessEqual:
    case value::Comparison::GreaterEqual:
        return constant;
    case value::Comparison::NotEqual:
        return constant == 0 ? T{1} : T{0};
    case value::Comparison::Less:
        return constant == -infinity ? std::nullopt : std::optional(std::nextafter(constant, -infinity));
    case value::Comparison::Greater:
        return constant == infinity ? std::nullopt : std::optional(std::nextafter(constant, infinity));
    }
    std::unreachable();
}

std::optional<value::Value> satisfying(ast::AstContext& context, const Bound& bound) {
    if (sema::isInteger(context, bound.type)) {
        return satisfyingInteger(bound.op, bound.constant, sema::integerFormat(context, bound.type));
    }
    switch (sema::floatFormat(context, bound.type)) {
    case value::FloatFormat::Float:
        return satisfyingFloating(bound.op, bound.constant.asFloat()).transform(value::Value::ofFloat);
    case value::FloatFormat::Double:
        return satisfyingFloating(bound.op, bound.constant.asDouble()).transform(value::Value::ofDouble);
    case value::FloatFormat::LongDouble:
        return satisfyingFloating(bound.op, bound.constant.asLongDouble()).transform(value::Value::ofLongDouble);
    }
    std::unreachable();
}

} // namespace

value::Comparison negated(value::Comparison op) {
    switch (op) {
    case value::Comparison::Equal:
        return value::Comparison::NotEqual;
    case value::Comparison::NotEqual:
        return value::Comparison::Equal;
    case value::Comparison::Less:
        return value::Comparison::GreaterEqual;
    case value::Comparison::LessEqual:
        return value::Comparison::Greater;
    case value::Comparison::Greater:
        return value::Comparison::LessEqual;
    case value::Comparison::GreaterEqual:
        return value::Comparison::Less;
    }
    std::unreachable();
}

value::Comparison mirrored(value::Comparison op) {
    switch (op) {
    case value::Comparison::Equal:
    case value::Comparison::NotEqual:
        return op;
    case value::Comparison::Less:
        return value::Comparison::Greater;
    case value::Comparison::LessEqual:
        return value::Comparison::GreaterEqual;
    case value::Comparison::Greater:
        return value::Comparison::Less;
    case value::Comparison::GreaterEqual:
        return value::Comparison::LessEqual;
    }
    std::unreachable();
}

bool isArithmetic(ast::AstContext& context, ast::QualType type) {
    return sema::isArithmetic(context, type);
}

value::Value valueOf(ast::AstContext& context, Int128 number, ast::QualType type) {
    const value::Value integer = value::Value::integer(static_cast<UInt128>(number), {.bits = 128, .isSigned = true});
    const std::optional<value::Value> converted =
        convert(context, integer, context.builtin(ast::BuiltinKind::Int128), type);
    NITWIT_ASSERT(converted.has_value(), "an integer constant converts to every arithmetic type");
    return *converted;
}

std::optional<value::Value> convert(ast::AstContext& context, const value::Value& value, ast::QualType from,
                                    ast::QualType to) {
    if (!isArithmetic(context, from) || !isArithmetic(context, to)) {
        return std::nullopt;
    }
    if (isBool(context, to)) {
        return value::toBool(value); // C17 6.3.1.2
    }
    const bool fromInteger = sema::isInteger(context, from);
    if (sema::isInteger(context, to)) {
        if (fromInteger) {
            return value::convertInteger(value, sema::integerFormat(context, to));
        }
        const value::Result result = value::floatToInteger(
            value, sema::floatFormat(context, from), sema::integerFormat(context, to), context.target().dataModel());
        if (result.undefinedBehavior != value::UndefinedBehavior::None) {
            return std::nullopt;
        }
        return result.value;
    }
    if (fromInteger) {
        return value::integerToFloat(value, sema::integerFormat(context, from), sema::floatFormat(context, to));
    }
    return value::convertFloat(value, sema::floatFormat(context, from), sema::floatFormat(context, to));
}

std::string toString(ast::AstContext& context, const value::Value& value, ast::QualType type) {
    switch (value.kind()) {
    case value::Value::Kind::Integer: {
        if (sema::isPointer(context, type)) {
            return std::format("{:#x}", static_cast<std::uint64_t>(value.bits()));
        }
        const bool isNegative = sema::isSignedInteger(context, type) && value.signedBits() < 0;
        UInt128 magnitude = isNegative ? UInt128{0} - value.bits() : value.bits();
        std::string digits;
        do {
            digits.insert(digits.begin(), static_cast<char>('0' + static_cast<int>(magnitude % 10)));
            magnitude /= 10;
        } while (magnitude != 0);
        return isNegative ? "-" + digits : digits;
    }
    case value::Value::Kind::Float:
        return std::format("{}", value.asFloat());
    case value::Value::Kind::Double:
        return std::format("{}", value.asDouble());
    case value::Value::Kind::LongDouble:
        return std::format("{}", value.asLongDouble());
    case value::Value::Kind::None:
    case value::Value::Kind::Bytes:
        break;
    }
    NITWIT_UNREACHABLE("inputs are scalars");
}

bool satisfies(ast::AstContext& context, const value::Value& value, const Bound& bound) {
    if (sema::isInteger(context, bound.type)) {
        return value::integerCompare(bound.op, value, bound.constant, sema::integerFormat(context, bound.type));
    }
    return value::floatCompare(bound.op, value, bound.constant, sema::floatFormat(context, bound.type));
}

std::optional<value::Value> choose(ast::AstContext& context, std::span<const Bound> bounds, ast::QualType type) {
    if (!isArithmetic(context, type)) {
        return std::nullopt;
    }
    // A candidate is checked after the conversions the program applies: from the input's type to each comparison's.
    const auto satisfiesAll = [&](const value::Value& candidate) {
        for (const Bound& bound : bounds) {
            const std::optional<value::Value> compared = convert(context, candidate, type, bound.type);
            if (!compared || !satisfies(context, *compared, bound)) {
                return false;
            }
        }
        return true;
    };
    for (const Bound& bound : bounds) {
        if (!isArithmetic(context, bound.type)) {
            return std::nullopt;
        }
        const std::optional<value::Value> wanted = satisfying(context, bound);
        if (!wanted) {
            continue;
        }
        const std::optional<value::Value> candidate = convert(context, *wanted, bound.type, type);
        if (candidate && satisfiesAll(*candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace nitwit::validate
