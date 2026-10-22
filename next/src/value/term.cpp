#include "value/term.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "support/assert.hpp"

namespace nitwit::value {

// A run may record millions of terms (TermLimits::maxTerms).
static_assert(sizeof(Term) <= 32, "terms must stay compact");

namespace {

/// The type of the result of comparisons, `!`, `&&` and `||`: int (C17 6.5.3.3p5, 6.5.8p6, 6.5.9p3, 6.5.13p3,
/// 6.5.14p3).
constexpr IntegerFormat intFormat{.bits = 32, .isSigned = true};

std::span<const TermId> operandsOf(const Term& term) {
    return std::span(term.operands).first(term.operandCount);
}

IntegerFormat integerFormat(const ScalarFormat& format) {
    const auto* integer = std::get_if<IntegerFormat>(&format);
    NITWIT_ASSERT(integer != nullptr, "the term has a floating format where an integer format belongs");
    return *integer;
}

FloatFormat floatFormat(const ScalarFormat& format) {
    const auto* floating = std::get_if<FloatFormat>(&format);
    NITWIT_ASSERT(floating != nullptr, "the term has an integer format where a floating format belongs");
    return *floating;
}

/// Whether `value` is a value of `format`, in canonical form if it is an integer.
bool hasFormat(const Value& value, const ScalarFormat& format) {
    if (const auto* integer = std::get_if<IntegerFormat>(&format)) {
        return value.kind() == Value::Kind::Integer && Value::integer(value.bits(), *integer).bits() == value.bits();
    }
    switch (std::get<FloatFormat>(format)) {
    case FloatFormat::Float:
        return value.kind() == Value::Kind::Float;
    case FloatFormat::Double:
        return value.kind() == Value::Kind::Double;
    case FloatFormat::LongDouble:
        return value.kind() == Value::Kind::LongDouble;
    }
    NITWIT_UNREACHABLE("unknown float format");
}

/// `value` without what the run knows about it: the values a table stores and evaluates have neither term nor origin.
Value detached(Value value) {
    value.setTerm(std::nullopt);
    value.setOrigin(std::nullopt);
    return value;
}

std::optional<Value> definedValue(const Result& result) {
    if (result.undefinedBehavior != UndefinedBehavior::None) {
        return std::nullopt;
    }
    return result.value;
}

Value truthValue(bool holds) {
    return Value::integer(holds ? 1u : 0u, intFormat);
}

/// Whether a scalar compares unequal to 0, which is what `!`, `&&`, `||` and `?:` test (C17 6.5.3.3p5, 6.5.13p3,
/// 6.5.14p3, 6.5.15p4).
bool isTrue(const Value& value) {
    return toBool(value).bits() != 0;
}

bool isConversion(TermKind kind) {
    return kind == TermKind::ConvertInteger || kind == TermKind::IntegerToFloat || kind == TermKind::FloatToInteger
           || kind == TermKind::ConvertFloat;
}

} // namespace

// ---- Substitution ----

void Substitution::set(InputId input, Value value) {
    const auto entry = std::ranges::find(entries_, input, &std::pair<InputId, Value>::first);
    if (entry != entries_.end()) {
        entry->second = detached(std::move(value));
    } else {
        entries_.emplace_back(input, detached(std::move(value)));
    }
}

const Value* Substitution::find(InputId input) const {
    const auto entry = std::ranges::find(entries_, input, &std::pair<InputId, Value>::first);
    return entry == entries_.end() ? nullptr : &entry->second;
}

// ---- Building ----

TermTable::TermTable(DataModel model, TermLimits limits) : model_(model), limits_(limits) {}

void TermTable::clear() {
    terms_.clear();
    constants_.clear();
    inputValues_.clear();
}

std::optional<TermId> TermTable::input(InputId id, const Value& value, ScalarFormat format) {
    NITWIT_ASSERT(hasFormat(value, format), "the input's value does not have the input's format");
    if (!hasRoomFor(1)) {
        return std::nullopt;
    }
    const std::uint32_t index = std::to_underlying(id);
    if (index >= inputValues_.size()) {
        inputValues_.resize(index + std::size_t{1});
    }
    NITWIT_ASSERT(inputValues_[index].kind() == Value::Kind::None, "an input is registered twice in one run");
    inputValues_[index] = detached(value);
    return add({.kind = TermKind::Input, .format = format, .payload = index});
}

std::optional<TermId> TermTable::integerBinary(IntegerOperator op, const Value& lhs, const Value& rhs,
                                               IntegerFormat format) {
    return record({.kind = TermKind::IntegerBinary, .op = std::to_underlying(op), .format = format}, {&lhs, &rhs});
}

std::optional<TermId> TermTable::shift(ShiftOperator op, const Value& lhs, IntegerFormat lhsFormat, const Value& rhs,
                                       IntegerFormat rhsFormat) {
    return record(
        {.kind = TermKind::Shift, .op = std::to_underlying(op), .format = lhsFormat, .secondFormat = rhsFormat},
        {&lhs, &rhs});
}

std::optional<TermId> TermTable::integerNegate(const Value& operand, IntegerFormat format) {
    return record({.kind = TermKind::IntegerNegate, .format = format}, {&operand});
}

std::optional<TermId> TermTable::integerComplement(const Value& operand, IntegerFormat format) {
    return record({.kind = TermKind::IntegerComplement, .format = format}, {&operand});
}

std::optional<TermId> TermTable::integerCompare(Comparison comparison, const Value& lhs, const Value& rhs,
                                                IntegerFormat format) {
    return record({.kind = TermKind::IntegerCompare, .op = std::to_underlying(comparison), .format = format},
                  {&lhs, &rhs});
}

std::optional<TermId> TermTable::floatBinary(FloatOperator op, const Value& lhs, const Value& rhs, FloatFormat format) {
    return record({.kind = TermKind::FloatBinary, .op = std::to_underlying(op), .format = format}, {&lhs, &rhs});
}

std::optional<TermId> TermTable::floatNegate(const Value& operand, FloatFormat format) {
    return record({.kind = TermKind::FloatNegate, .format = format}, {&operand});
}

std::optional<TermId> TermTable::floatCompare(Comparison comparison, const Value& lhs, const Value& rhs,
                                              FloatFormat format) {
    return record({.kind = TermKind::FloatCompare, .op = std::to_underlying(comparison), .format = format},
                  {&lhs, &rhs});
}

std::optional<TermId> TermTable::convertInteger(const Value& operand, IntegerFormat from, IntegerFormat to) {
    return record({.kind = TermKind::ConvertInteger, .format = from, .secondFormat = to}, {&operand});
}

std::optional<TermId> TermTable::toBool(const Value& operand, ScalarFormat from) {
    return record({.kind = TermKind::ToBool, .format = from}, {&operand});
}

std::optional<TermId> TermTable::integerToFloat(const Value& operand, IntegerFormat from, FloatFormat to) {
    return record({.kind = TermKind::IntegerToFloat, .format = from, .secondFormat = to}, {&operand});
}

std::optional<TermId> TermTable::floatToInteger(const Value& operand, FloatFormat from, IntegerFormat to) {
    return record({.kind = TermKind::FloatToInteger, .format = from, .secondFormat = to}, {&operand});
}

std::optional<TermId> TermTable::convertFloat(const Value& operand, FloatFormat from, FloatFormat to) {
    return record({.kind = TermKind::ConvertFloat, .format = from, .secondFormat = to}, {&operand});
}

std::optional<TermId> TermTable::logicalNot(const Value& operand, ScalarFormat format) {
    return record({.kind = TermKind::LogicalNot, .format = format}, {&operand});
}

std::optional<TermId> TermTable::logicalAnd(const Value& lhs, ScalarFormat lhsFormat, const Value* rhs,
                                            ScalarFormat rhsFormat) {
    const Term term{.kind = TermKind::LogicalAnd, .format = lhsFormat, .secondFormat = rhsFormat};
    return rhs == nullptr ? record(term, {&lhs}) : record(term, {&lhs, rhs});
}

std::optional<TermId> TermTable::logicalOr(const Value& lhs, ScalarFormat lhsFormat, const Value* rhs,
                                           ScalarFormat rhsFormat) {
    const Term term{.kind = TermKind::LogicalOr, .format = lhsFormat, .secondFormat = rhsFormat};
    return rhs == nullptr ? record(term, {&lhs}) : record(term, {&lhs, rhs});
}

std::optional<TermId> TermTable::conditional(const Value& condition, ScalarFormat conditionFormat, bool conditionHeld,
                                             const Value& selected) {
    return record({.kind = TermKind::Conditional,
                   .op = static_cast<std::uint8_t>(conditionHeld ? 1 : 0),
                   .format = conditionFormat},
                  {&condition, &selected});
}

std::optional<TermId> TermTable::record(Term term, std::initializer_list<const Value*> operands) {
    unsigned deepest = 0;
    bool hasTerm = false;
    std::size_t constants = 0;
    for (const Value* operand : operands) {
        if (const std::optional<TermId> id = operand->term()) {
            deepest = std::max<unsigned>(deepest, this->term(*id).depth);
            hasTerm = true;
        } else {
            ++constants;
        }
    }
    // Checked before anything is added, so that a result without a term leaves no constants behind.
    if (!hasTerm || deepest + 1 > limits_.maxDepth || !hasRoomFor(constants + 1)) {
        return std::nullopt;
    }
    term.depth = static_cast<std::uint16_t>(deepest + 1);
    term.operandCount = static_cast<std::uint8_t>(operands.size());
    for (std::size_t i = 0; const Value* operand : operands) {
        if (const std::optional<TermId> id = operand->term()) {
            term.operands[i] = *id;
        } else {
            constants_.push_back(detached(*operand));
            term.operands[i] =
                add({.kind = TermKind::Constant, .payload = static_cast<std::uint32_t>(constants_.size() - 1)});
        }
        ++i;
    }
    return add(term);
}

TermId TermTable::add(const Term& term) {
    terms_.push_back(term);
    return TermId{static_cast<std::uint32_t>(terms_.size() - 1)};
}

// ---- Reading ----

const Term& TermTable::term(TermId id) const {
    const std::uint32_t index = std::to_underlying(id);
    NITWIT_ASSERT(index < terms_.size(), "the term is not in this table, or from before clear()");
    return terms_[index];
}

InputId TermTable::inputOf(const Term& term) const {
    NITWIT_ASSERT(term.kind == TermKind::Input, "the term is not an input");
    return InputId{term.payload};
}

const Value& TermTable::constantOf(const Term& term) const {
    NITWIT_ASSERT(term.kind == TermKind::Constant, "the term is not a constant");
    NITWIT_ASSERT(term.payload < constants_.size(), "the constant is not in this table");
    return constants_[term.payload];
}

std::vector<InputId> TermTable::inputs(TermId id) const {
    std::vector<InputId> result;
    std::unordered_set<std::uint32_t> seen{std::to_underlying(id)};
    std::vector<TermId> pending{id};
    while (!pending.empty()) {
        const Term& current = term(pending.back());
        pending.pop_back();
        if (current.kind == TermKind::Input) {
            result.push_back(inputOf(current)); // unique: each input has at most one term per run
        }
        for (const TermId operand : operandsOf(current)) {
            if (seen.insert(std::to_underlying(operand)).second) {
                pending.push_back(operand);
            }
        }
    }
    std::ranges::sort(result);
    return result;
}

std::optional<InputId> TermTable::origin(TermId id) const {
    const Term* current = &term(id);
    while (isConversion(current->kind)) {
        current = &term(current->operands[0]);
    }
    if (current->kind != TermKind::Input) {
        return std::nullopt;
    }
    return inputOf(*current);
}

// ---- Evaluation ----

std::optional<Value> TermTable::evaluate(TermId id, const Substitution& substitution) const {
    // Shared subterms would make a recursive evaluation exponential, and terms may be TermLimits::maxDepth deep: each
    // term is evaluated once, after its operands, with an explicit stack.
    std::unordered_map<std::uint32_t, std::optional<Value>> values;
    const auto isEvaluated = [&](TermId term) {
        return values.contains(std::to_underlying(term));
    };
    std::vector<TermId> pending{id};
    while (!pending.empty()) {
        const TermId current = pending.back();
        if (isEvaluated(current)) {
            pending.pop_back();
            continue;
        }
        const Term& currentTerm = term(current);
        const std::size_t before = pending.size();
        for (const TermId operand : operandsOf(currentTerm)) {
            if (!isEvaluated(operand)) {
                pending.push_back(operand);
            }
        }
        if (pending.size() > before) {
            continue; // back here once the operands are evaluated
        }
        std::array<const std::optional<Value>*, 3> operands{};
        for (std::size_t i = 0; const TermId operand : operandsOf(currentTerm)) {
            operands[i++] = &values.at(std::to_underlying(operand));
        }
        values.emplace(std::to_underlying(current),
                       evaluateTerm(currentTerm, std::span(operands).first(currentTerm.operandCount), substitution));
        pending.pop_back();
    }
    return std::move(values.at(std::to_underlying(id)));
}

std::optional<Value> TermTable::evaluateTerm(const Term& term, std::span<const std::optional<Value>* const> operands,
                                             const Substitution& substitution) const {
    switch (term.kind) {
    case TermKind::Input:
        if (const Value* substituted = substitution.find(inputOf(term))) {
            NITWIT_ASSERT(hasFormat(*substituted, term.format), "a substituted value does not have the input's format");
            return *substituted;
        }
        return inputValues_[term.payload];
    case TermKind::Constant:
        return constantOf(term);
    case TermKind::LogicalAnd:
    case TermKind::LogicalOr: {
        // C17 6.5.13p4, 6.5.14p4: the right operand is evaluated only if the left one does not decide the result.
        const bool decidingValue = term.kind == TermKind::LogicalOr;
        if (!*operands[0]) {
            return std::nullopt;
        }
        if (isTrue(**operands[0]) == decidingValue) {
            return truthValue(decidingValue);
        }
        if (operands.size() < 2 || !*operands[1]) {
            return std::nullopt;
        }
        return truthValue(isTrue(**operands[1]));
    }
    case TermKind::Conditional:
        // The term has only the operand the run evaluated, which the condition must select again.
        if (!*operands[0] || isTrue(**operands[0]) != (term.op == 1)) {
            return std::nullopt;
        }
        return *operands[1];
    default:
        break;
    }

    // The remaining kinds have no value if an operand has none.
    if (std::ranges::any_of(operands, [](const std::optional<Value>* operand) { return !*operand; })) {
        return std::nullopt;
    }
    const auto operand = [&](std::size_t i) -> const Value& {
        return **operands[i];
    };
    switch (term.kind) {
    case TermKind::IntegerBinary:
        return definedValue(value::integerBinary(static_cast<IntegerOperator>(term.op), operand(0), operand(1),
                                                 integerFormat(term.format)));
    case TermKind::Shift:
        return definedValue(value::shift(static_cast<ShiftOperator>(term.op), operand(0), integerFormat(term.format),
                                         operand(1), integerFormat(term.secondFormat)));
    case TermKind::IntegerNegate:
        return definedValue(value::integerNegate(operand(0), integerFormat(term.format)));
    case TermKind::IntegerComplement:
        return value::integerComplement(operand(0), integerFormat(term.format));
    case TermKind::IntegerCompare:
        return truthValue(value::integerCompare(static_cast<Comparison>(term.op), operand(0), operand(1),
                                                integerFormat(term.format)));
    case TermKind::FloatBinary:
        return value::floatBinary(static_cast<FloatOperator>(term.op), operand(0), operand(1),
                                  floatFormat(term.format));
    case TermKind::FloatNegate:
        return value::floatNegate(operand(0), floatFormat(term.format));
    case TermKind::FloatCompare:
        return truthValue(
            value::floatCompare(static_cast<Comparison>(term.op), operand(0), operand(1), floatFormat(term.format)));
    case TermKind::ConvertInteger:
        return value::convertInteger(operand(0), integerFormat(term.secondFormat));
    case TermKind::ToBool:
        return value::toBool(operand(0));
    case TermKind::IntegerToFloat:
        return value::integerToFloat(operand(0), integerFormat(term.format), floatFormat(term.secondFormat));
    case TermKind::FloatToInteger:
        return definedValue(
            value::floatToInteger(operand(0), floatFormat(term.format), integerFormat(term.secondFormat), model_));
    case TermKind::ConvertFloat:
        return value::convertFloat(operand(0), floatFormat(term.format), floatFormat(term.secondFormat));
    case TermKind::LogicalNot:
        return truthValue(!isTrue(operand(0)));
    case TermKind::Input:
    case TermKind::Constant:
    case TermKind::LogicalAnd:
    case TermKind::LogicalOr:
    case TermKind::Conditional:
        break;
    }
    NITWIT_UNREACHABLE("unknown term kind");
}

} // namespace nitwit::value
