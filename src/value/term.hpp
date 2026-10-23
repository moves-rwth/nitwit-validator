#pragma once

// Terms: how a run-time value was computed from the nondeterministic inputs of a run (value/README.md, "Terms"). The
// interpreter records a term for each value it derives from an input through the operations of arithmetic.hpp; the
// validator evaluates terms with other input values to choose the inputs of the next run (validate/README.md,
// "Search").

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "support/target.hpp"
#include "value/arithmetic.hpp"
#include "value/value.hpp"

namespace nitwit::value {

/// The format of a scalar: integers and pointers, or floating values.
using ScalarFormat = std::variant<IntegerFormat, FloatFormat>;

/// The operation a term records. The operands are terms; the formats are the format arguments of the arithmetic.hpp
/// function the kind is named after, in its order (`Term::format`, then `Term::secondFormat`).
enum class TermKind : std::uint8_t {
    Input,             // an input of the run; format: the input's
    Constant,          // a value that depends on no input; format: its own
    Opaque,            // a value that depends on inputs in a way the table does not record (TermTable::opaque)
    IntegerBinary,     // op: IntegerOperator; format
    Shift,             // op: ShiftOperator; format: lhsFormat, secondFormat: rhsFormat
    IntegerNegate,     // format
    IntegerComplement, // format
    IntegerCompare,    // op: Comparison; format: the operands'; the result is an int, 0 or 1
    FloatBinary,       // op: FloatOperator; format
    FloatNegate,       // format
    FloatCompare,      // op: Comparison; format: the operands'; the result is an int, 0 or 1
    ConvertInteger,    // format: from, secondFormat: to
    ToBool,            // format: from
    IntegerToFloat,    // format: from, secondFormat: to
    FloatToInteger,    // format: from, secondFormat: to
    ConvertFloat,      // format: from, secondFormat: to
    LogicalNot,        // `!a`; format: a's; the result is an int, 0 or 1
    LogicalAnd,        // `a && b`; formats: a's, b's; one operand if the run did not evaluate b
    LogicalOr,         // `a || b`; as LogicalAnd
    Conditional,       // `c ? x : y`; operands: c, the evaluated operand (op 1: x, 0: y); formats: c's, the result's
};

/// The members are ordered so that a term takes 32 bytes.
struct Term {
    TermKind kind = TermKind::Constant;
    std::uint8_t op = 0;     // see TermKind
    std::uint16_t depth = 0; // the length of the longest path to an input or a constant
    std::uint8_t operandCount = 0;
    ScalarFormat format{};
    ScalarFormat secondFormat{};
    /// Recorded in a TermTable::StrictScope: any undefined behaviour of the operation ends the evaluation.
    bool strict = false;
    std::array<TermId, 3> operands{}; // the first operandCount are valid
    std::uint32_t payload = 0;        // Input: the InputId; Constant: the index of its value in the table
    std::uint32_t inputValue = 0;     // Input: the index of the run's value in the table
};

struct TermLimits {
    /// The number of terms of a run, beyond which only inputs and conversions of inputs get terms (each conversion of
    /// an input once), up to twice the number; 0 records none.
    std::uint32_t maxTerms = 1U << 21;
    /// A result whose term would be deeper gets the opaque term, so that loops like `x = x * 31 + c` stay cheap.
    std::uint16_t maxDepth = 64;

    /// Twice `maxTerms`: the size of the table up to which inputs get terms, and the bound of the InputIds that get
    /// one. An input numbered this or higher gets the opaque term (TermTable::input), whatever the order of the
    /// requests, so that the validator need not keep later inputs for the search.
    [[nodiscard]] std::size_t maxInputs() const { return 2 * std::size_t{maxTerms}; }
};

/// Input values that replace the values of the run when terms are evaluated.
class Substitution {
public:
    void set(InputId input, Value value);
    /// The value of `input`; null if the substitution keeps the run's value.
    [[nodiscard]] const Value* find(InputId input) const;
    [[nodiscard]] std::span<const std::pair<InputId, Value>> entries() const { return entries_; }
    [[nodiscard]] bool empty() const { return entries_.empty(); }

private:
    std::vector<std::pair<InputId, Value>> entries_; // few entries: a linear search is fastest
};

/// Why a term has no value (TermTable::evaluate).
enum class NoValue : std::uint8_t {
    Unknown, // the value depends on the opaque term or on an operand the run did not evaluate
    EndsRun, // the program stops at undefined behaviour (endsRun; any undefined behaviour of a strict term)
};

/// The terms of one run. Terms only refer to earlier terms, so a table is a DAG in creation order.
class TermTable {
public:
    /// While a scope is alive, the table records strict terms: the terms of a witness constraint, whose evaluation
    /// stops at any undefined behaviour, where the program continues after some (interp/README.md, "Undefined
    /// behaviour"). A scope with `isStrict` false suspends an outer one, for terms that belong to the program although
    /// a constraint records them, such as those of an input that a constraint reads first.
    class StrictScope {
    public:
        explicit StrictScope(TermTable& table, bool isStrict = true) : table_(table), previous_(table.strict_) {
            table.strict_ = isStrict;
        }
        ~StrictScope() { table_.strict_ = previous_; }
        StrictScope(const StrictScope&) = delete;
        StrictScope& operator=(const StrictScope&) = delete;

    private:
        TermTable& table_;
        bool previous_;
    };

    explicit TermTable(DataModel model, TermLimits limits = {});

    /// Forgets all terms; at the start of a run.
    void clear();

    // ---- Building, by the interpreter ----
    //
    // Each function takes the arguments of the arithmetic.hpp function that computed the result, and returns the
    // result's term, or nullopt if no operand has a term (Value::term()). That check is inline, so that operations on
    // values without terms cost no call. An operand without a term is recorded as a constant of its value, which must
    // be a value of the operand's format. A result with an opaque operand, or whose term would exceed a limit
    // (TermLimits), gets the opaque term. A conversion of an input exceeds the size limit only at 2 * `maxTerms`, as
    // inputs do; past the size limit, the same conversion of the same input gives the same term.

    /// Registers the run's value of an input, at most once per run, in any order. Inputs get a term even in a full
    /// table, so that the search can change them, until the table holds 2 * `maxTerms` terms; after that they get the
    /// opaque term, so that a loop reading inputs without end does not exhaust the memory. Inputs numbered
    /// `TermLimits::maxInputs` or higher always get the opaque term. Nullopt only if `maxTerms` is 0.
    [[nodiscard]] std::optional<TermId> input(InputId id, const Value& value, ScalarFormat format);
    [[nodiscard]] std::optional<TermId> integerBinary(IntegerOperator op, const Value& lhs, const Value& rhs,
                                                      IntegerFormat format);
    [[nodiscard]] std::optional<TermId> shift(ShiftOperator op, const Value& lhs, IntegerFormat lhsFormat,
                                              const Value& rhs, IntegerFormat rhsFormat);
    [[nodiscard]] std::optional<TermId> integerNegate(const Value& operand, IntegerFormat format);
    [[nodiscard]] std::optional<TermId> integerComplement(const Value& operand, IntegerFormat format);
    [[nodiscard]] std::optional<TermId> integerCompare(Comparison comparison, const Value& lhs, const Value& rhs,
                                                       IntegerFormat format);
    [[nodiscard]] std::optional<TermId> floatBinary(FloatOperator op, const Value& lhs, const Value& rhs,
                                                    FloatFormat format);
    [[nodiscard]] std::optional<TermId> floatNegate(const Value& operand, FloatFormat format);
    [[nodiscard]] std::optional<TermId> floatCompare(Comparison comparison, const Value& lhs, const Value& rhs,
                                                     FloatFormat format);
    [[nodiscard]] std::optional<TermId> convertInteger(const Value& operand, IntegerFormat from, IntegerFormat to);
    [[nodiscard]] std::optional<TermId> toBool(const Value& operand, ScalarFormat from);
    [[nodiscard]] std::optional<TermId> integerToFloat(const Value& operand, IntegerFormat from, FloatFormat to);
    [[nodiscard]] std::optional<TermId> floatToInteger(const Value& operand, FloatFormat from, IntegerFormat to);
    [[nodiscard]] std::optional<TermId> convertFloat(const Value& operand, FloatFormat from, FloatFormat to);
    [[nodiscard]] std::optional<TermId> logicalNot(const Value& operand, ScalarFormat format);
    /// `rhs` is null exactly if the run did not evaluate it, because `lhs` decided the result.
    [[nodiscard]] std::optional<TermId> logicalAnd(const Value& lhs, ScalarFormat lhsFormat, const Value* rhs,
                                                   ScalarFormat rhsFormat);
    [[nodiscard]] std::optional<TermId> logicalOr(const Value& lhs, ScalarFormat lhsFormat, const Value* rhs,
                                                  ScalarFormat rhsFormat);
    /// `selected` is the operand the run evaluated, the one `condition` selects, a scalar of `resultFormat`. With a
    /// condition without term, the result is `selected`'s term: the condition selects the same operand for every input.
    [[nodiscard]] std::optional<TermId> conditional(const Value& condition, ScalarFormat conditionFormat,
                                                    const Value& selected, ScalarFormat resultFormat);

    /// The term of a value that depends on inputs in a way the table does not record (through library functions,
    /// pointer arithmetic or partial overwrites, for example), so that the value does not become a constant of later
    /// terms. Nullopt if `maxTerms` is 0.
    [[nodiscard]] std::optional<TermId> opaque() const {
        return limits_.maxTerms > 0 ? std::optional(opaqueTerm) : std::nullopt;
    }

    // ---- Reading, by the validator ----

    [[nodiscard]] const Term& term(TermId id) const;
    [[nodiscard]] InputId inputOf(const Term& term) const;         // precondition: kind Input
    [[nodiscard]] const Value& constantOf(const Term& term) const; // precondition: kind Constant
    /// The number of terms, including the opaque term.
    [[nodiscard]] std::size_t size() const { return terms_.size(); }

    /// Whether a term is the opaque term. No term has the opaque term as an operand (an operation on it gives the
    /// opaque term), so any other term depends only on its inputs. A requirement on the opaque term is unknown: the
    /// validator cannot evaluate it and changes no input for it.
    [[nodiscard]] bool isOpaque(TermId id) const;
    /// The inputs a term depends on, in ascending order; none for the opaque term.
    [[nodiscard]] std::vector<InputId> inputs(TermId id) const;
    /// The terms that `id` depends on, and `id`, in ascending order: every operand comes before the terms that use it.
    [[nodiscard]] std::vector<TermId> subterms(TermId id) const;
    /// The format of a term's value: an int for comparisons, `!`, `&&` and `||`, a _Bool for ToBool, the target format
    /// of conversions and the result format of Conditional, else the term's format. Precondition: not the opaque term.
    [[nodiscard]] ScalarFormat resultFormat(TermId id) const;
    /// The value of a term when the inputs in `substitution` have those values instead of the run's; with an empty
    /// substitution, the value the run computed. Undefined behaviour gives the value the run continues with
    /// (arithmetic.hpp, endsRun).
    ///
    /// No value (NoValue::EndsRun) if the program would stop at undefined behaviour: one that ends the run, or any in
    /// the operation of a strict term. No value (NoValue::Unknown) for the opaque term and for an operand the run did
    /// not evaluate: the right operand of `&&` and `||`, the other operand of `?:`. The program evaluates every
    /// operand of an arithmetic operation, comparison or conversion, so one that ends the run ends the evaluation even
    /// if another one is unknown; of `&&`, `||` and `?:`, only the operands the program would evaluate count, in
    /// order.
    [[nodiscard]] std::expected<Value, NoValue> evaluate(TermId id, const Substitution& substitution) const;

private:
    /// An operand of a term being recorded, and its format, for when it becomes a constant.
    struct Operand {
        const Value& value;
        ScalarFormat format;
    };

    static constexpr TermId opaqueTerm{0}; // the first term of every table with maxTerms > 0

    /// The size of the table up to which inputs, and conversions of inputs, get terms: twice the size limit.
    [[nodiscard]] std::size_t inputCap() const { return limits_.maxInputs(); }
    /// Records `term` with `operands`, of which at least one has a term.
    [[nodiscard]] std::optional<TermId> record(Term term, std::initializer_list<Operand> operands);
    [[nodiscard]] std::optional<TermId> recordLogical(TermKind kind, const Value& lhs, ScalarFormat lhsFormat,
                                                      const Value* rhs, ScalarFormat rhsFormat);
    [[nodiscard]] std::optional<TermId> recordConditional(const Value& condition, ScalarFormat conditionFormat,
                                                          const Value& selected, ScalarFormat resultFormat);
    TermId add(const Term& term);
    /// Stores a value of a constant or input and returns its index.
    std::uint32_t addValue(const Value& value);
    /// The value of `term` from the values of its operands.
    [[nodiscard]] std::expected<Value, NoValue>
    evaluateTerm(const Term& term, std::span<const std::expected<Value, NoValue>* const> operands,
                 const Substitution& substitution) const;

    DataModel model_;
    TermLimits limits_;
    bool strict_ = false; // in a StrictScope
    std::vector<Term> terms_;
    std::vector<Value> values_;    // the values of constants and the run's values of inputs
    std::vector<bool> registered_; // by InputId: whether the input has a term
    /// Past the size limit, the conversions of inputs, by input, kind, formats and strictness (record()).
    std::unordered_map<std::uint64_t, TermId> inputConversions_;
};

// The building functions check for operands with terms inline.

inline std::optional<TermId> TermTable::integerBinary(IntegerOperator op, const Value& lhs, const Value& rhs,
                                                      IntegerFormat format) {
    if (!lhs.term() && !rhs.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::IntegerBinary, .op = std::to_underlying(op), .format = format},
                  {{lhs, format}, {rhs, format}});
}

inline std::optional<TermId> TermTable::shift(ShiftOperator op, const Value& lhs, IntegerFormat lhsFormat,
                                              const Value& rhs, IntegerFormat rhsFormat) {
    if (!lhs.term() && !rhs.term()) {
        return std::nullopt;
    }
    return record(
        {.kind = TermKind::Shift, .op = std::to_underlying(op), .format = lhsFormat, .secondFormat = rhsFormat},
        {{lhs, lhsFormat}, {rhs, rhsFormat}});
}

inline std::optional<TermId> TermTable::integerNegate(const Value& operand, IntegerFormat format) {
    if (!operand.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::IntegerNegate, .format = format}, {{operand, format}});
}

inline std::optional<TermId> TermTable::integerComplement(const Value& operand, IntegerFormat format) {
    if (!operand.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::IntegerComplement, .format = format}, {{operand, format}});
}

inline std::optional<TermId> TermTable::integerCompare(Comparison comparison, const Value& lhs, const Value& rhs,
                                                       IntegerFormat format) {
    if (!lhs.term() && !rhs.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::IntegerCompare, .op = std::to_underlying(comparison), .format = format},
                  {{lhs, format}, {rhs, format}});
}

inline std::optional<TermId> TermTable::floatBinary(FloatOperator op, const Value& lhs, const Value& rhs,
                                                    FloatFormat format) {
    if (!lhs.term() && !rhs.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::FloatBinary, .op = std::to_underlying(op), .format = format},
                  {{lhs, format}, {rhs, format}});
}

inline std::optional<TermId> TermTable::floatNegate(const Value& operand, FloatFormat format) {
    if (!operand.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::FloatNegate, .format = format}, {{operand, format}});
}

inline std::optional<TermId> TermTable::floatCompare(Comparison comparison, const Value& lhs, const Value& rhs,
                                                     FloatFormat format) {
    if (!lhs.term() && !rhs.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::FloatCompare, .op = std::to_underlying(comparison), .format = format},
                  {{lhs, format}, {rhs, format}});
}

inline std::optional<TermId> TermTable::convertInteger(const Value& operand, IntegerFormat from, IntegerFormat to) {
    if (!operand.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::ConvertInteger, .format = from, .secondFormat = to}, {{operand, from}});
}

inline std::optional<TermId> TermTable::toBool(const Value& operand, ScalarFormat from) {
    if (!operand.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::ToBool, .format = from}, {{operand, from}});
}

inline std::optional<TermId> TermTable::integerToFloat(const Value& operand, IntegerFormat from, FloatFormat to) {
    if (!operand.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::IntegerToFloat, .format = from, .secondFormat = to}, {{operand, from}});
}

inline std::optional<TermId> TermTable::floatToInteger(const Value& operand, FloatFormat from, IntegerFormat to) {
    if (!operand.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::FloatToInteger, .format = from, .secondFormat = to}, {{operand, from}});
}

inline std::optional<TermId> TermTable::convertFloat(const Value& operand, FloatFormat from, FloatFormat to) {
    if (!operand.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::ConvertFloat, .format = from, .secondFormat = to}, {{operand, from}});
}

inline std::optional<TermId> TermTable::logicalNot(const Value& operand, ScalarFormat format) {
    if (!operand.term()) {
        return std::nullopt;
    }
    return record({.kind = TermKind::LogicalNot, .format = format}, {{operand, format}});
}

inline std::optional<TermId> TermTable::logicalAnd(const Value& lhs, ScalarFormat lhsFormat, const Value* rhs,
                                                   ScalarFormat rhsFormat) {
    if (!lhs.term() && (rhs == nullptr || !rhs->term())) {
        return std::nullopt;
    }
    return recordLogical(TermKind::LogicalAnd, lhs, lhsFormat, rhs, rhsFormat);
}

inline std::optional<TermId> TermTable::logicalOr(const Value& lhs, ScalarFormat lhsFormat, const Value* rhs,
                                                  ScalarFormat rhsFormat) {
    if (!lhs.term() && (rhs == nullptr || !rhs->term())) {
        return std::nullopt;
    }
    return recordLogical(TermKind::LogicalOr, lhs, lhsFormat, rhs, rhsFormat);
}

inline std::optional<TermId> TermTable::conditional(const Value& condition, ScalarFormat conditionFormat,
                                                    const Value& selected, ScalarFormat resultFormat) {
    if (!condition.term()) {
        return selected.term();
    }
    return recordConditional(condition, conditionFormat, selected, resultFormat);
}

} // namespace nitwit::value
