// Terms of values (interp/README.md, "Terms"): opaque bytes in memory, and the consistency check of
// RunOptions::checkTerms. The evaluator records the terms where it applies the operations of arithmetic.hpp.

#include <array>
#include <bit>
#include <expected>
#include <format>
#include <string_view>
#include <utility>

#include "interp/execution.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

std::string_view operatorName(value::IntegerOperator op) {
    static constexpr std::array<std::string_view, 8> names = {"+", "-", "*", "/", "%", "&", "|", "^"};
    return names.at(std::to_underlying(op));
}

std::string_view operatorName(value::FloatOperator op) {
    static constexpr std::array<std::string_view, 4> names = {"+", "-", "*", "/"};
    return names.at(std::to_underlying(op));
}

std::string_view comparisonName(value::Comparison comparison) {
    static constexpr std::array<std::string_view, 6> names = {"==", "!=", "<", "<=", ">", ">="};
    return names.at(std::to_underlying(comparison));
}

/// The operation a term records, for the messages of the check.
std::string operationOf(const value::Term& term) {
    switch (term.kind) {
    case value::TermKind::Input:
        return "an input";
    case value::TermKind::Constant:
        return "a constant";
    case value::TermKind::Opaque:
        return "the opaque term";
    case value::TermKind::IntegerBinary:
        return std::format("integer {}", operatorName(static_cast<value::IntegerOperator>(term.op)));
    case value::TermKind::Shift:
        return term.op == std::to_underlying(value::ShiftOperator::Left) ? "<<" : ">>";
    case value::TermKind::IntegerNegate:
        return "integer negation";
    case value::TermKind::IntegerComplement:
        return "~";
    case value::TermKind::IntegerCompare:
        return std::format("integer {}", comparisonName(static_cast<value::Comparison>(term.op)));
    case value::TermKind::FloatBinary:
        return std::format("floating {}", operatorName(static_cast<value::FloatOperator>(term.op)));
    case value::TermKind::FloatNegate:
        return "floating negation";
    case value::TermKind::FloatCompare:
        return std::format("floating {}", comparisonName(static_cast<value::Comparison>(term.op)));
    case value::TermKind::ConvertInteger:
        return "an integer conversion";
    case value::TermKind::ToBool:
        return "a conversion to _Bool";
    case value::TermKind::IntegerToFloat:
        return "an integer to floating conversion";
    case value::TermKind::FloatToInteger:
        return "a floating to integer conversion";
    case value::TermKind::ConvertFloat:
        return "a floating conversion";
    case value::TermKind::LogicalNot:
        return "!";
    case value::TermKind::LogicalAnd:
        return "&&";
    case value::TermKind::LogicalOr:
        return "||";
    case value::TermKind::Conditional:
        return "?:";
    }
    NITWIT_UNREACHABLE("unknown term kind");
}

/// The x87 bytes of a long double, without padding.
std::array<std::byte, 16> bytesOf(const value::Value& value) {
    std::array<std::byte, 16> bytes{};
    value::encodeFloat(value, value::FloatFormat::LongDouble, bytes);
    return bytes;
}

/// Whether two scalars are the same value bit for bit, so that NaNs and the signs of zeros count, and integers are in
/// the same canonical form.
bool isSame(const value::Value& a, const value::Value& b) {
    if (a.kind() != b.kind()) {
        return false;
    }
    switch (a.kind()) {
    case value::Value::Kind::Integer:
        return a.bits() == b.bits();
    case value::Value::Kind::Float:
        return std::bit_cast<std::uint32_t>(a.asFloat()) == std::bit_cast<std::uint32_t>(b.asFloat());
    case value::Value::Kind::Double:
        return std::bit_cast<std::uint64_t>(a.asDouble()) == std::bit_cast<std::uint64_t>(b.asDouble());
    case value::Value::Kind::LongDouble:
        return bytesOf(a) == bytesOf(b);
    case value::Value::Kind::None:
    case value::Value::Kind::Bytes:
        break;
    }
    return false;
}

std::string describe(const value::Value& value) {
    switch (value.kind()) {
    case value::Value::Kind::Integer: {
        // The canonical 128 bits.
        const auto high = static_cast<std::uint64_t>(value.bits() >> 64);
        const auto low = static_cast<std::uint64_t>(value.bits());
        return high == 0 ? std::format("{:#x}", low) : std::format("{:#x}{:016x}", high, low);
    }
    case value::Value::Kind::Float:
        return std::format("{} (float)", value.asFloat());
    case value::Value::Kind::Double:
        return std::format("{} (double)", value.asDouble());
    case value::Value::Kind::LongDouble:
        return std::format("{} (long double)", value.asLongDouble());
    case value::Value::Kind::None:
        return "no value";
    case value::Value::Kind::Bytes:
        return "bytes";
    }
    return "?";
}

} // namespace

std::optional<std::string> termMismatch(const value::TermTable& terms, value::TermId term, const value::Value& value) {
    if (terms.isOpaque(term)) {
        return std::nullopt;
    }
    const std::expected<value::Value, value::NoValue> evaluated = terms.evaluate(term, value::Substitution{});
    if (evaluated && isSame(*evaluated, value)) {
        return std::nullopt;
    }
    std::string actual = "an unknown value";
    if (evaluated) {
        actual = describe(*evaluated);
    } else if (evaluated.error() == value::NoValue::EndsRun) {
        actual = "the end of the run";
    }
    return std::format("the term of {} gives {}, but the run computed {}", operationOf(terms.term(term)), actual,
                       describe(value));
}

void Execution::checkTerm(const value::Value& value, const ast::Expr* where) const {
    if (const std::optional<std::string> mismatch = termMismatch(terms_, *value.term(), value)) {
        stop(Termination::Unsupported, "internal error: " + *mismatch, locationOf(where));
    }
}

void Execution::markOpaque(Address address, std::uint64_t size) {
    if (recordsTerms()) {
        memory_.markOpaque(address, size);
    }
}

} // namespace nitwit::interp
