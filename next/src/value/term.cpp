#include "value/term.hpp"

#include <algorithm>

#include "support/assert.hpp"

namespace nitwit::value {

// A run may record millions of terms (TermLimits::maxTerms).
static_assert(sizeof(Term) <= 32, "terms must stay compact");

namespace {

using Outcome = std::expected<Value, NoValue>;

/// The type of the result of comparisons, `!`, `&&` and `||`: int (C17 6.5.3.3p5, 6.5.8p6, 6.5.9p3, 6.5.13p3,
/// 6.5.14p3).
constexpr IntegerFormat intFormat{.bits = 32, .isSigned = true};
/// The format of _Bool, which ToBool converts to (C17 6.3.1.2).
constexpr IntegerFormat boolFormat{.bits = 8, .isSigned = false};

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

/// `value` without its term: the values a table stores and evaluates have none.
Value detached(Value value) {
    value.setTerm(std::nullopt);
    return value;
}

/// The value an operation gives the program: none if the program stops at its undefined behaviour, which in a strict
/// term is any.
Outcome runValue(const Result& result, bool strict) {
    if (result.undefinedBehavior != UndefinedBehavior::None && (strict || endsRun(result.undefinedBehavior))) {
        return std::unexpected(NoValue::EndsRun);
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

/// The kinds through which a term keeps the origin of its operand.
bool keepsOrigin(TermKind kind) {
    return kind == TermKind::ConvertInteger || kind == TermKind::ToBool || kind == TermKind::IntegerToFloat
           || kind == TermKind::FloatToInteger || kind == TermKind::ConvertFloat;
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

TermTable::TermTable(DataModel model, TermLimits limits) : model_(model), limits_(limits) {
    clear();
}

void TermTable::clear() {
    terms_.clear();
    values_.clear();
    registered_.clear();
    if (limits_.maxTerms > 0) {
        add({.kind = TermKind::Opaque});
    }
}

std::optional<TermId> TermTable::input(InputId id, const Value& value, ScalarFormat format) {
    NITWIT_ASSERT(hasFormat(value, format), "the input's value does not have the input's format");
    if (limits_.maxTerms == 0) {
        return std::nullopt;
    }
    if (terms_.size() >= 2 * std::size_t{limits_.maxTerms}) {
        return opaque();
    }
    const std::uint32_t index = std::to_underlying(id);
    if (index >= registered_.size()) {
        registered_.resize(index + std::size_t{1});
    }
    NITWIT_ASSERT(!registered_[index], "an input is registered twice in one run");
    registered_[index] = true;
    return add({.kind = TermKind::Input, .format = format, .payload = index, .inputValue = addValue(value)});
}

std::optional<TermId> TermTable::record(Term term, std::initializer_list<Operand> operands) {
    unsigned deepest = 0;
    std::size_t constants = 0;
    for (const Operand& operand : operands) {
        if (const std::optional<TermId> id = operand.value.term()) {
            const Term& operandTerm = this->term(*id);
            if (operandTerm.kind == TermKind::Opaque) {
                return id;
            }
            deepest = std::max<unsigned>(deepest, operandTerm.depth);
        } else {
            NITWIT_ASSERT(hasFormat(operand.value, operand.format), "a constant operand is not a value of its format");
            ++constants;
        }
    }
    // Checked before anything is added, so that a result over a limit leaves no constants behind.
    if (deepest + 1 > limits_.maxDepth || !hasRoomFor(constants + 1)) {
        return opaque();
    }
    term.depth = static_cast<std::uint16_t>(deepest + 1);
    term.operandCount = static_cast<std::uint8_t>(operands.size());
    term.strict = strict_;
    for (std::size_t i = 0; const Operand& operand : operands) {
        const std::optional<TermId> id = operand.value.term();
        term.operands[i++] =
            id ? *id : add({.kind = TermKind::Constant, .format = operand.format, .payload = addValue(operand.value)});
    }
    return add(term);
}

std::optional<TermId> TermTable::recordLogical(TermKind kind, const Value& lhs, ScalarFormat lhsFormat,
                                               const Value* rhs, ScalarFormat rhsFormat) {
    // C17 6.5.13p4, 6.5.14p4: the right operand is evaluated only if the left one does not decide the result.
    const bool lhsDecides = isTrue(lhs) == (kind == TermKind::LogicalOr);
    NITWIT_ASSERT((rhs == nullptr) == lhsDecides, "the right operand of && or || is given unless the left one decides");
    const Term term{.kind = kind, .format = lhsFormat, .secondFormat = rhsFormat};
    if (rhs == nullptr) {
        return record(term, {{lhs, lhsFormat}});
    }
    return record(term, {{lhs, lhsFormat}, {*rhs, rhsFormat}});
}

std::optional<TermId> TermTable::recordConditional(const Value& condition, ScalarFormat conditionFormat,
                                                   const Value& selected, ScalarFormat resultFormat) {
    return record({.kind = TermKind::Conditional,
                   .op = static_cast<std::uint8_t>(isTrue(condition) ? 1 : 0),
                   .format = conditionFormat,
                   .secondFormat = resultFormat},
                  {{condition, conditionFormat}, {selected, resultFormat}});
}

TermId TermTable::add(const Term& term) {
    terms_.push_back(term);
    return TermId{static_cast<std::uint32_t>(terms_.size() - 1)};
}

std::uint32_t TermTable::addValue(const Value& value) {
    values_.push_back(detached(value));
    return static_cast<std::uint32_t>(values_.size() - 1);
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
    NITWIT_ASSERT(term.payload < values_.size(), "the constant is not in this table");
    return values_[term.payload];
}

bool TermTable::isOpaque(TermId id) const {
    return term(id).kind == TermKind::Opaque;
}

std::vector<InputId> TermTable::inputs(TermId id) const {
    std::vector<InputId> result;
    for (const TermId subterm : subterms(id)) {
        if (const Term& current = term(subterm); current.kind == TermKind::Input) {
            result.push_back(inputOf(current)); // unique: an input has one term per run
        }
    }
    std::ranges::sort(result);
    return result;
}

std::vector<TermId> TermTable::subterms(TermId id) const {
    // Operands have smaller ids than the terms that use them. Taking the largest pending term each time takes the
    // copies of a term, one per use, one after another, so that each term is listed once.
    std::vector<TermId> result;
    std::vector<TermId> pending{id}; // a max-heap
    while (!pending.empty()) {
        std::ranges::pop_heap(pending);
        const TermId current = pending.back();
        pending.pop_back();
        if (!result.empty() && result.back() == current) {
            continue; // another path to the same term
        }
        result.push_back(current);
        for (const TermId operand : operandsOf(term(current))) {
            pending.push_back(operand);
            std::ranges::push_heap(pending);
        }
    }
    std::ranges::reverse(result);
    return result;
}

ScalarFormat TermTable::resultFormat(TermId id) const {
    const Term& current = term(id);
    switch (current.kind) {
    case TermKind::IntegerCompare:
    case TermKind::FloatCompare:
    case TermKind::LogicalNot:
    case TermKind::LogicalAnd:
    case TermKind::LogicalOr:
        return intFormat;
    case TermKind::ToBool:
        return boolFormat;
    case TermKind::ConvertInteger:
    case TermKind::IntegerToFloat:
    case TermKind::FloatToInteger:
    case TermKind::ConvertFloat:
    case TermKind::Conditional:
        return current.secondFormat;
    case TermKind::Opaque:
        NITWIT_UNREACHABLE("the opaque term has no value");
    case TermKind::Input:
    case TermKind::Constant:
    case TermKind::IntegerBinary:
    case TermKind::Shift:
    case TermKind::IntegerNegate:
    case TermKind::IntegerComplement:
    case TermKind::FloatBinary:
    case TermKind::FloatNegate:
        break;
    }
    return current.format;
}

std::optional<InputId> TermTable::origin(TermId id) const {
    const Term* current = &term(id);
    while (keepsOrigin(current->kind)) {
        current = &term(current->operands[0]);
    }
    if (current->kind != TermKind::Input) {
        return std::nullopt;
    }
    return inputOf(*current);
}

// ---- Evaluation ----

std::expected<Value, NoValue> TermTable::evaluate(TermId id, const Substitution& substitution) const {
    // Shared subterms would make a recursive evaluation exponential, and terms may be TermLimits::maxDepth deep: each
    // subterm is evaluated once, in ascending order, without recursion.
    const std::vector<TermId> order = subterms(id);
    std::vector<Outcome> outcomes;
    outcomes.reserve(order.size()); // the operands point into it
    for (const TermId current : order) {
        const Term& currentTerm = term(current);
        std::array<const Outcome*, 3> operands{};
        for (std::size_t i = 0; const TermId operand : operandsOf(currentTerm)) {
            operands[i++] =
                &outcomes[static_cast<std::size_t>(std::ranges::lower_bound(order, operand) - order.begin())];
        }
        outcomes.push_back(
            evaluateTerm(currentTerm, std::span(operands).first(currentTerm.operandCount), substitution));
    }
    return std::move(outcomes.back());
}

std::expected<Value, NoValue> TermTable::evaluateTerm(const Term& term,
                                                      std::span<const std::expected<Value, NoValue>* const> operands,
                                                      const Substitution& substitution) const {
    switch (term.kind) {
    case TermKind::Input:
        if (const Value* substituted = substitution.find(inputOf(term))) {
            NITWIT_ASSERT(hasFormat(*substituted, term.format), "a substituted value does not have the input's format");
            return *substituted;
        }
        return values_[term.inputValue];
    case TermKind::Constant:
        return constantOf(term);
    case TermKind::Opaque:
        return std::unexpected(NoValue::Unknown);
    case TermKind::LogicalAnd:
    case TermKind::LogicalOr: {
        // C17 6.5.13p4, 6.5.14p4: the right operand is evaluated only if the left one does not decide the result.
        const Outcome& lhs = *operands[0];
        if (!lhs) {
            return lhs;
        }
        const bool decidingValue = term.kind == TermKind::LogicalOr;
        if (isTrue(*lhs) == decidingValue) {
            return truthValue(decidingValue);
        }
        if (operands.size() < 2) {
            return std::unexpected(NoValue::Unknown);
        }
        const Outcome& rhs = *operands[1];
        if (!rhs) {
            return rhs;
        }
        return truthValue(isTrue(*rhs));
    }
    case TermKind::Conditional: {
        // The term has only the operand the run evaluated, which the condition must select again.
        const Outcome& condition = *operands[0];
        if (!condition) {
            return condition;
        }
        if (isTrue(*condition) != (term.op == 1)) {
            return std::unexpected(NoValue::Unknown);
        }
        return *operands[1];
    }
    default:
        break;
    }

    // The program evaluates every operand of the remaining kinds: if one ends the run, so does the operation.
    const auto anyOperandFails = [&](NoValue reason) {
        return std::ranges::any_of(operands,
                                   [&](const Outcome* operand) { return !*operand && operand->error() == reason; });
    };
    if (anyOperandFails(NoValue::EndsRun)) {
        return std::unexpected(NoValue::EndsRun);
    }
    if (anyOperandFails(NoValue::Unknown)) {
        return std::unexpected(NoValue::Unknown);
    }
    const auto operand = [&](std::size_t i) -> const Value& {
        return **operands[i];
    };
    switch (term.kind) {
    case TermKind::IntegerBinary:
        return runValue(value::integerBinary(static_cast<IntegerOperator>(term.op), operand(0), operand(1),
                                             integerFormat(term.format)),
                        term.strict);
    case TermKind::Shift:
        return runValue(value::shift(static_cast<ShiftOperator>(term.op), operand(0), integerFormat(term.format),
                                     operand(1), integerFormat(term.secondFormat)),
                        term.strict);
    case TermKind::IntegerNegate:
        return runValue(value::integerNegate(operand(0), integerFormat(term.format)), term.strict);
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
        return runValue(
            value::floatToInteger(operand(0), floatFormat(term.format), integerFormat(term.secondFormat), model_),
            term.strict);
    case TermKind::ConvertFloat:
        return value::convertFloat(operand(0), floatFormat(term.format), floatFormat(term.secondFormat));
    case TermKind::LogicalNot:
        return truthValue(!isTrue(operand(0)));
    case TermKind::Input:
    case TermKind::Constant:
    case TermKind::Opaque:
    case TermKind::LogicalAnd:
    case TermKind::LogicalOr:
    case TermKind::Conditional:
        break;
    }
    NITWIT_UNREACHABLE("unknown term kind");
}

} // namespace nitwit::value
