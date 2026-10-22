#pragma once

// Terms: how a run-time value was computed from the nondeterministic inputs of a run (value/README.md, "Terms"). The
// interpreter records a term for each value it derives from an input through the operations of arithmetic.hpp; the
// validator evaluates terms with other input values to choose the inputs of the next run (validate/README.md,
// "Search").

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
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
    Constant,          // a value that depends on no input
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
    Conditional,       // `c ? x : y`; operands: c and the operand the run evaluated; op: 1 if that is x; format: c's
};

struct Term {
    TermKind kind = TermKind::Constant;
    std::uint8_t op = 0;     // see TermKind
    std::uint16_t depth = 0; // the length of the longest path to an input or a constant
    std::uint8_t operandCount = 0;
    std::array<TermId, 3> operands{}; // the first operandCount are valid
    ScalarFormat format{};
    ScalarFormat secondFormat{};
    std::uint32_t payload = 0; // Input: the InputId; Constant: the index of its value in the table
};

struct TermLimits {
    /// The number of terms of a run; 0 records none.
    std::uint32_t maxTerms = 1U << 21;
    /// A result whose term would be deeper gets none, so that loops like `x = x * 31 + c` stay cheap.
    std::uint16_t maxDepth = 64;
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

/// The terms of one run. Terms only refer to earlier terms, so a table is a DAG in creation order.
class TermTable {
public:
    explicit TermTable(DataModel model, TermLimits limits = {});

    /// Forgets all terms; at the start of a run.
    void clear();

    // ---- Building, by the interpreter ----
    //
    // Each function takes the arguments of the arithmetic.hpp function that computed the result, and returns the
    // result's term: nullopt if no operand has a term (Value::term()) or a limit is reached. An operand without a term
    // is recorded as a constant of its value.

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
    /// `rhs` is null if the run did not evaluate it.
    [[nodiscard]] std::optional<TermId> logicalAnd(const Value& lhs, ScalarFormat lhsFormat, const Value* rhs,
                                                   ScalarFormat rhsFormat);
    [[nodiscard]] std::optional<TermId> logicalOr(const Value& lhs, ScalarFormat lhsFormat, const Value* rhs,
                                                  ScalarFormat rhsFormat);
    /// `selected` is the operand the run evaluated: the second if `conditionHeld`, else the third.
    [[nodiscard]] std::optional<TermId> conditional(const Value& condition, ScalarFormat conditionFormat,
                                                    bool conditionHeld, const Value& selected);

    // ---- Reading, by the validator ----

    [[nodiscard]] const Term& term(TermId id) const;
    [[nodiscard]] InputId inputOf(const Term& term) const;         // precondition: kind Input
    [[nodiscard]] const Value& constantOf(const Term& term) const; // precondition: kind Constant
    [[nodiscard]] std::size_t size() const { return terms_.size(); }

    /// The inputs a term depends on, in ascending order.
    [[nodiscard]] std::vector<InputId> inputs(TermId id) const;
    /// The input a term is derived from through conversions between arithmetic types only (ConvertInteger,
    /// IntegerToFloat, FloatToInteger, ConvertFloat); nullopt for any other term.
    [[nodiscard]] std::optional<InputId> origin(TermId id) const;

    /// The value of a term when the inputs in `substitution` have those values instead of the run's; with an empty
    /// substitution, the value the run computed. Nullopt if the evaluation has undefined behaviour (arithmetic.hpp's
    /// UndefinedBehavior) or needs an operand the run did not evaluate (the right operand of `&&` and `||`, the other
    /// operand of `?:`).
    [[nodiscard]] std::optional<Value> evaluate(TermId id, const Substitution& substitution) const;

private:
    [[nodiscard]] bool hasRoomFor(std::size_t count) const { return terms_.size() + count <= limits_.maxTerms; }
    /// Adds `term` with `operands`, recording those without a term as constants; nullopt as for the functions above.
    [[nodiscard]] std::optional<TermId> record(Term term, std::initializer_list<const Value*> operands);
    TermId add(const Term& term);
    /// The value of `term` from the values of its operands, which are empty where an operand has none.
    [[nodiscard]] std::optional<Value> evaluateTerm(const Term& term,
                                                    std::span<const std::optional<Value>* const> operands,
                                                    const Substitution& substitution) const;

    DataModel model_;
    TermLimits limits_;
    std::vector<Term> terms_;
    std::vector<Value> constants_;   // by Term::payload
    std::vector<Value> inputValues_; // by InputId: the values of the run; void for inputs without a term
};

} // namespace nitwit::value
