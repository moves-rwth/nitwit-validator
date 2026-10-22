#include "validate/search.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ast/dump.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"
#include "validate/values.hpp"

namespace nitwit::validate {

namespace {

using value::Comparison;
using value::InputId;
using value::NoValue;
using value::ScalarFormat;
using value::Term;
using value::TermId;
using value::TermKind;
using value::Value;

// Bounds that keep the cost of solving an atom a small multiple of evaluating it.
constexpr std::size_t maxInputsPerAtom = 8; // the inputs tried for one atom, in the order of preference
constexpr std::size_t maxConstants = 8;     // the constants of an atom that give candidates
constexpr int textDepth = 5;                // the levels of a term in the log

/// The value of an atom or clause with some input values.
enum class Truth : std::uint8_t { False, True, Unknown, EndsRun };

Truth missingTruth(NoValue missing) {
    return missing == NoValue::EndsRun ? Truth::EndsRun : Truth::Unknown;
}

/// The builtin type of a format: the search computes in formats, values.hpp in types. ToBool is the only conversion to
/// _Bool, so {8, unsigned} is taken as unsigned char.
ast::QualType typeOf(ast::AstContext& context, const ScalarFormat& format) {
    if (const auto* floating = std::get_if<value::FloatFormat>(&format)) {
        switch (*floating) {
        case value::FloatFormat::Float:
            return context.builtin(ast::BuiltinKind::Float);
        case value::FloatFormat::Double:
            return context.builtin(ast::BuiltinKind::Double);
        case value::FloatFormat::LongDouble:
            return context.builtin(ast::BuiltinKind::LongDouble);
        }
    }
    const auto integer = std::get<value::IntegerFormat>(format);
    switch (integer.bits) {
    case 8:
        return context.builtin(integer.isSigned ? ast::BuiltinKind::SignedChar : ast::BuiltinKind::UnsignedChar);
    case 16:
        return context.builtin(integer.isSigned ? ast::BuiltinKind::Short : ast::BuiltinKind::UnsignedShort);
    case 32:
        return context.builtin(integer.isSigned ? ast::BuiltinKind::Int : ast::BuiltinKind::UnsignedInt);
    case 64:
        return context.builtin(integer.isSigned ? ast::BuiltinKind::LongLong : ast::BuiltinKind::UnsignedLongLong);
    case 128:
        return context.builtin(integer.isSigned ? ast::BuiltinKind::Int128 : ast::BuiltinKind::UnsignedInt128);
    default:
        NITWIT_UNREACHABLE("integer formats have 8 to 128 bits");
    }
}

bool compare(Comparison op, const Value& lhs, const Value& rhs, const ScalarFormat& format) {
    if (const auto* integer = std::get_if<value::IntegerFormat>(&format)) {
        return value::integerCompare(op, lhs, rhs, *integer);
    }
    return value::floatCompare(op, lhs, rhs, std::get<value::FloatFormat>(format));
}

bool isTrue(const Value& value) {
    return value::toBool(value).bits() != 0;
}

/// Whether two scalars have the same representation (also for NaN and -0.0).
bool isSame(const Value& a, const Value& b) {
    if (a.kind() != b.kind()) {
        return false;
    }
    switch (a.kind()) {
    case Value::Kind::Integer:
        return a.bits() == b.bits();
    case Value::Kind::Float:
        return std::bit_cast<std::uint32_t>(a.asFloat()) == std::bit_cast<std::uint32_t>(b.asFloat());
    case Value::Kind::Double:
        return std::bit_cast<std::uint64_t>(a.asDouble()) == std::bit_cast<std::uint64_t>(b.asDouble());
    case Value::Kind::LongDouble: {
        std::array<std::byte, 16> first{};
        std::array<std::byte, 16> second{};
        value::encodeFloat(a, value::FloatFormat::LongDouble, first);
        value::encodeFloat(b, value::FloatFormat::LongDouble, second);
        return first == second;
    }
    case Value::Kind::None:
    case Value::Kind::Bytes:
        break;
    }
    return false;
}

/// The inverse of an odd number modulo 2^128, by Newton's iteration: each step doubles the number of correct low bits,
/// starting with 3 (a * a == 1 modulo 8 for odd a).
UInt128 inverseOfOdd(UInt128 a) {
    UInt128 inverse = a;
    for (int i = 0; i < 6; ++i) {
        inverse *= 2 - a * inverse;
    }
    return inverse;
}

std::string_view nameOf(Comparison op) {
    switch (op) {
    case Comparison::Equal:
        return "==";
    case Comparison::NotEqual:
        return "!=";
    case Comparison::Less:
        return "<";
    case Comparison::LessEqual:
        return "<=";
    case Comparison::Greater:
        return ">";
    case Comparison::GreaterEqual:
        return ">=";
    }
    std::unreachable();
}

// Indexed by the enumerators.
std::string_view nameOf(value::IntegerOperator op) {
    static constexpr std::array<std::string_view, 8> names{"+", "-", "*", "/", "%", "&", "|", "^"};
    return names[std::to_underlying(op)];
}

std::string_view nameOf(value::FloatOperator op) {
    static constexpr std::array<std::string_view, 4> names{"+", "-", "*", "/"};
    return names[std::to_underlying(op)];
}

/// Solves one requirement greedily over its clauses (validate/README.md, "Search").
class Solver {
public:
    Solver(ast::AstContext& context, const value::TermTable& terms, std::span<const SearchInput> inputs)
        : context_(context), terms_(terms), inputs_(inputs) {}

    /// Makes `clauses` guards: no new value may make them false.
    void keep(std::span<const Clause> clauses) {
        for (const Clause& clause : clauses) {
            guards_.push_back(guardOf(clause, false));
        }
    }

    SearchResult solve(std::span<const Clause> requirement);

private:
    /// A clause that a new value must not make end the run, nor false unless `mayBeFalse`.
    struct Guard {
        Clause clause;
        std::vector<InputId> inputs; // the inputs its atoms depend on, ascending
        bool mayBeFalse = false;
    };

    /// `lhs op rhs` in `format`; the right side is a term or, without one, `constant`.
    struct Relation {
        TermId lhs{};
        Comparison op = Comparison::Equal;
        std::optional<TermId> rhs;
        Value constant;
        ScalarFormat format;
    };

    enum class Outcome : std::uint8_t {
        Holds,
        Unknown, // holds as far as the run evaluated the operands
        Fails,
    };

    struct Choice {
        Value value;
        Change::Method method = Change::Method::Inversion;
    };

    /// The values chosen so far, to restore after an alternative failed.
    struct Snapshot {
        value::Substitution chosen;
        std::vector<std::pair<InputId, Change::Method>> methods;
    };

    [[nodiscard]] Truth truthOf(const Atom& atom, const value::Substitution& substitution) const;
    [[nodiscard]] Truth truthOf(std::span<const Atom> clause, const value::Substitution& substitution) const;
    [[nodiscard]] Truth truthOf(const Relation& relation, const value::Substitution& substitution) const;

    Outcome solveAtom(const Atom& atom);
    /// Makes `term` non-zero (`wantTrue`) or zero, decomposing `!`, conversions to _Bool, `&&`, `||` and comparisons.
    Outcome solveGoal(TermId term, bool wantTrue);
    bool solveRelation(const Relation& relation);
    /// The value of input `input` that gives `side op bound`, through the operations on the path to it.
    [[nodiscard]] std::optional<Value> invert(TermId side, Comparison op, const Value& bound,
                                              const ScalarFormat& format, InputId input) const;
    /// The value an operand of `term` must have so that `term` has the value `wanted`.
    [[nodiscard]] std::optional<Value> preimage(TermId term, std::size_t operand, const Value& wanted) const;
    [[nodiscard]] std::vector<Value> candidates(InputId input, const Relation& relation) const;
    /// Takes `choice` for `input` if it gives the relation and keeps the guards.
    bool tryValue(InputId input, const Choice& choice, const Relation& relation);

    /// The inputs of `term` the search may change, in the order of preference.
    [[nodiscard]] std::vector<InputId> changeable(const std::vector<InputId>& ids) const;
    [[nodiscard]] Guard guardOf(const Clause& clause, bool mayBeFalse) const;
    [[nodiscard]] Value constantIn(Int128 number, const ScalarFormat& format) const {
        return valueOf(context_, number, typeOf(context_, format));
    }
    [[nodiscard]] std::optional<Value> offset(const Value& value, Int128 delta, ast::QualType type) const;
    [[nodiscard]] Snapshot snapshot() const { return {.chosen = chosen_, .methods = methods_}; }
    void restore(Snapshot snapshot) {
        chosen_ = std::move(snapshot.chosen);
        methods_ = std::move(snapshot.methods);
    }

    [[nodiscard]] std::string textOf(TermId id, int depth, bool isTop) const;
    [[nodiscard]] std::string textOf(const Clause& clause) const;

    ast::AstContext& context_;
    const value::TermTable& terms_;
    std::span<const SearchInput> inputs_;
    value::Substitution chosen_;
    std::vector<std::pair<InputId, Change::Method>> methods_; // how each input in chosen_ got its value
    std::vector<Guard> guards_;
};

SearchResult Solver::solve(std::span<const Clause> requirement) {
    std::string needs;
    const auto failure = [&] {
        return SearchResult{.changes = {}, .explanation = std::format("needs {}: no input values found", needs)};
    };
    for (const Clause& clause : requirement) {
        if (truthOf(clause, chosen_) != Truth::True) {
            needs += std::format("{}`{}`", needs.empty() ? "" : ", ", textOf(clause));
            // While the clause is solved, no value may make it end the run.
            guards_.push_back(guardOf(clause, true));
            bool isSolved = false;
            for (const Atom& atom : clause) {
                Snapshot before = snapshot();
                if (solveAtom(atom) != Outcome::Fails) {
                    const Truth after = truthOf(clause, chosen_);
                    isSolved = after == Truth::True || after == Truth::Unknown;
                }
                if (isSolved) {
                    break;
                }
                restore(std::move(before));
            }
            guards_.pop_back();
            if (!isSolved && truthOf(clause, chosen_) != Truth::Unknown) {
                return failure();
            }
        }
        guards_.push_back(guardOf(clause, false)); // the values for later clauses keep it
    }

    SearchResult result;
    for (const auto& [input, value] : chosen_.entries()) {
        if (isSame(value, inputs_[std::to_underlying(input)].value)) {
            continue;
        }
        const auto method = std::ranges::find(methods_, input, &std::pair<InputId, Change::Method>::first)->second;
        result.changes.push_back({.input = input, .value = value, .method = method});
    }
    if (result.changes.empty()) {
        return failure();
    }
    result.explanation = std::format("needs {}", needs);
    for (const char* separator = "; "; const Change& change : result.changes) {
        result.explanation += std::format(
            "{}input {} := {} ({})", separator, std::to_underlying(change.input),
            toString(context_, change.value, inputs_[std::to_underlying(change.input)].type), nameOf(change.method));
        separator = ", ";
    }
    return result;
}

// ---- Evaluation ----

Truth Solver::truthOf(const Atom& atom, const value::Substitution& substitution) const {
    if (!atom.term || terms_.isOpaque(*atom.term)) {
        return Truth::Unknown;
    }
    const std::expected<Value, NoValue> value = terms_.evaluate(*atom.term, substitution);
    if (!value) {
        return missingTruth(value.error());
    }
    const ScalarFormat format = terms_.resultFormat(*atom.term);
    return compare(atom.op, *value, constantIn(atom.constant, format), format) ? Truth::True : Truth::False;
}

// A clause is evaluated like the disjunction of its atoms in order: one that holds decides, and one that ends the run
// ends it, as in the avoid constraint `c1 && c2` whose clause is `c1 == 0 || c2 == 0`.
Truth Solver::truthOf(std::span<const Atom> clause, const value::Substitution& substitution) const {
    Truth result = Truth::False;
    for (const Atom& atom : clause) {
        switch (const Truth truth = truthOf(atom, substitution)) {
        case Truth::True:
        case Truth::EndsRun:
            return truth;
        case Truth::Unknown:
            result = Truth::Unknown;
            break;
        case Truth::False:
            break;
        }
    }
    return result;
}

Truth Solver::truthOf(const Relation& relation, const value::Substitution& substitution) const {
    const std::expected<Value, NoValue> lhs = terms_.evaluate(relation.lhs, substitution);
    const std::expected<Value, NoValue> rhs =
        relation.rhs ? terms_.evaluate(*relation.rhs, substitution) : relation.constant;
    if (!lhs || !rhs) {
        const bool endsRun = (!lhs && lhs.error() == NoValue::EndsRun) || (!rhs && rhs.error() == NoValue::EndsRun);
        return endsRun ? Truth::EndsRun : Truth::Unknown;
    }
    return compare(relation.op, *lhs, *rhs, relation.format) ? Truth::True : Truth::False;
}

// ---- Decomposition ----

Solver::Outcome Solver::solveAtom(const Atom& atom) {
    if (!atom.term || terms_.isOpaque(*atom.term)) {
        return Outcome::Fails; // unknown: no input is changed for it
    }
    const ScalarFormat format = terms_.resultFormat(*atom.term);
    if (atom.constant == 0 && (atom.op == Comparison::Equal || atom.op == Comparison::NotEqual)) {
        return solveGoal(*atom.term, atom.op == Comparison::NotEqual);
    }
    const Relation relation{.lhs = *atom.term,
                            .op = atom.op,
                            .rhs = std::nullopt,
                            .constant = constantIn(atom.constant, format),
                            .format = format};
    return solveRelation(relation) ? Outcome::Holds : Outcome::Fails;
}

Solver::Outcome Solver::solveGoal(TermId term, bool wantTrue) {
    const std::expected<Value, NoValue> now = terms_.evaluate(term, chosen_);
    if (now && isTrue(*now) == wantTrue) {
        return Outcome::Holds;
    }
    const Term& current = terms_.term(term);
    switch (current.kind) {
    case TermKind::Opaque:
        return Outcome::Fails;
    case TermKind::LogicalNot:
        return solveGoal(current.operands[0], !wantTrue);
    case TermKind::ToBool:
        return solveGoal(current.operands[0], wantTrue);
    case TermKind::IntegerCompare:
    case TermKind::FloatCompare: {
        // The negation of a floating comparison holds without NaN only; the relation is checked as such, which
        // implies the negation.
        const auto op = static_cast<Comparison>(current.op);
        const Relation relation{.lhs = current.operands[0],
                                .op = wantTrue ? op : negated(op),
                                .rhs = current.operands[1],
                                .constant = {},
                                .format = current.format};
        return solveRelation(relation) ? Outcome::Holds : Outcome::Fails;
    }
    case TermKind::LogicalAnd:
    case TermKind::LogicalOr: {
        const TermId first = current.operands[0];
        if ((current.kind == TermKind::LogicalAnd) != wantTrue) {
            // `a || b` true, `a && b` false: one operand decides, tried in order.
            Snapshot before = snapshot();
            if (const Outcome outcome = solveGoal(first, wantTrue); outcome != Outcome::Fails) {
                return outcome;
            }
            restore(std::move(before));
            return current.operandCount == 2 ? solveGoal(current.operands[1], wantTrue) : Outcome::Fails;
        }
        // `a && b` true, `a || b` false: both operands; the run did not evaluate a missing second one.
        const Outcome outcome = solveGoal(first, wantTrue);
        if (outcome == Outcome::Fails) {
            return outcome;
        }
        if (current.operandCount < 2) {
            return Outcome::Unknown;
        }
        guards_.push_back(guardOf(
            {{.term = first, .op = wantTrue ? Comparison::NotEqual : Comparison::Equal, .constant = 0}}, false));
        const Outcome second = solveGoal(current.operands[1], wantTrue);
        guards_.pop_back();
        if (second == Outcome::Fails) {
            return second;
        }
        return outcome == Outcome::Holds && second == Outcome::Holds ? Outcome::Holds : Outcome::Unknown;
    }
    default: {
        const ScalarFormat format = terms_.resultFormat(term);
        const Relation relation{.lhs = term,
                                .op = wantTrue ? Comparison::NotEqual : Comparison::Equal,
                                .rhs = std::nullopt,
                                .constant = constantIn(0, format),
                                .format = format};
        return solveRelation(relation) ? Outcome::Holds : Outcome::Fails;
    }
    }
}

// ---- Relations ----

// Each input of the relation in the order of preference gets the inverted value, if the relation can be inverted for
// it, and then candidates; values it was fixed to before come last. The first value that gives the relation and keeps
// the guards is taken.
bool Solver::solveRelation(const Relation& relation) {
    const std::vector<InputId> lhsInputs = terms_.inputs(relation.lhs);
    const std::vector<InputId> rhsInputs = relation.rhs ? terms_.inputs(*relation.rhs) : std::vector<InputId>{};
    std::vector<InputId> all;
    std::ranges::set_union(lhsInputs, rhsInputs, std::back_inserter(all));
    std::vector<InputId> order = changeable(all);
    if (order.size() > maxInputsPerAtom) {
        order.resize(maxInputsPerAtom);
    }
    for (const InputId input : order) {
        std::vector<Choice> choices;
        // Inversion needs the input on one side only; the other side is the bound.
        const bool isLeft = std::ranges::binary_search(lhsInputs, input);
        if (isLeft != std::ranges::binary_search(rhsInputs, input)) {
            const TermId side = isLeft ? relation.lhs : *relation.rhs;
            const std::expected<Value, NoValue> bound = !isLeft        ? terms_.evaluate(relation.lhs, chosen_)
                                                        : relation.rhs ? terms_.evaluate(*relation.rhs, chosen_)
                                                                       : relation.constant;
            if (bound) {
                if (std::optional<Value> inverted =
                        invert(side, isLeft ? relation.op : mirrored(relation.op), *bound, relation.format, input)) {
                    choices.push_back({.value = std::move(*inverted), .method = Change::Method::Inversion});
                }
            }
        }
        for (Value& candidate : candidates(input, relation)) {
            choices.push_back({.value = std::move(candidate), .method = Change::Method::Candidate});
        }
        std::vector<Choice> unique;
        for (Choice& choice : choices) {
            if (std::ranges::none_of(unique, [&](const Choice& other) { return isSame(other.value, choice.value); })) {
                unique.push_back(std::move(choice));
            }
        }
        const std::span<const Value> history = inputs_[std::to_underlying(input)].history;
        std::ranges::stable_partition(unique, [&](const Choice& choice) {
            return std::ranges::none_of(history, [&](const Value& old) { return isSame(old, choice.value); });
        });
        for (const Choice& choice : unique) {
            if (tryValue(input, choice, relation)) {
                return true;
            }
        }
    }
    return false;
}

bool Solver::tryValue(InputId input, const Choice& choice, const Relation& relation) {
    const SearchInput& info = inputs_[std::to_underlying(input)];
    // A value of the input's type: a pointer is not chosen, and a _Bool is 0 or 1.
    const std::optional<Value> converted = convert(context_, choice.value, info.type, info.type);
    if (!converted || !isSame(*converted, choice.value)) {
        return false;
    }
    value::Substitution trial = chosen_;
    trial.set(input, choice.value);
    if (truthOf(relation, trial) != Truth::True) {
        return false;
    }
    for (const Guard& guard : guards_) {
        if (!std::ranges::binary_search(guard.inputs, input)) {
            continue;
        }
        const Truth truth = truthOf(guard.clause, trial);
        if (truth == Truth::EndsRun || (truth == Truth::False && !guard.mayBeFalse)) {
            return false;
        }
    }
    chosen_ = std::move(trial);
    std::erase_if(methods_, [&](const auto& entry) { return entry.first == input; });
    methods_.emplace_back(input, choice.method);
    return true;
}

// ---- Inversion ----

std::optional<Value> Solver::invert(TermId side, Comparison op, const Value& bound, const ScalarFormat& format,
                                    InputId input) const {
    // The number of paths from `side` to each subterm (up to 2) and, for subterms with one path, the term and operand
    // it comes through, from `side` down in descending order.
    const std::vector<TermId> order = terms_.subterms(side);
    const auto indexOf = [&](TermId id) {
        return static_cast<std::size_t>(std::ranges::lower_bound(order, id) - order.begin());
    };
    std::vector<std::uint8_t> paths(order.size(), 0);
    std::vector<std::pair<std::size_t, std::size_t>> parent(order.size()); // the user's index and the operand
    std::optional<std::size_t> target;
    paths.back() = 1;
    for (std::size_t i = order.size(); i-- > 0;) {
        const Term& current = terms_.term(order[i]);
        if (current.kind == TermKind::Input && terms_.inputOf(current) == input) {
            target = i;
        }
        for (std::size_t k = 0; k < current.operandCount; ++k) {
            const std::size_t j = indexOf(current.operands[k]);
            paths[j] = static_cast<std::uint8_t>(std::min(2, paths[j] + paths[i]));
            parent[j] = {i, k};
        }
    }
    if (!target || paths[*target] != 1) {
        return std::nullopt; // the input occurs more than once
    }
    std::vector<std::pair<std::size_t, std::size_t>> steps; // from the input up: the user and the operand
    for (std::size_t i = *target; i + 1 != order.size(); i = parent[i].first) {
        steps.push_back(parent[i]);
    }

    const ast::QualType type = typeOf(context_, format);
    const Bound wanted{.op = op, .constant = bound, .type = type};
    std::optional<Value> value = choose(context_, std::span(&wanted, 1), type);
    for (auto step = steps.rbegin(); step != steps.rend() && value; ++step) {
        value = preimage(order[step->first], step->second, *value);
    }
    return value;
}

std::optional<Value> Solver::preimage(TermId term, std::size_t operand, const Value& wanted) const {
    const Term& current = terms_.term(term);
    switch (current.kind) {
    case TermKind::ConvertInteger:
    case TermKind::IntegerToFloat:
    case TermKind::FloatToInteger:
    case TermKind::ConvertFloat: {
        // The wanted value converted back, if the conversion gives it (truncation and rounding may not).
        const ast::QualType to = typeOf(context_, current.secondFormat);
        const Bound same{.op = Comparison::Equal, .constant = wanted, .type = to};
        return chooseInput(context_, std::span(&same, 1), to, typeOf(context_, current.format));
    }
    case TermKind::ToBool: {
        if (wanted.bits() > 1) {
            return std::nullopt;
        }
        const ast::QualType from = typeOf(context_, current.format);
        const Bound zero{.op = wanted.bits() == 1 ? Comparison::NotEqual : Comparison::Equal,
                         .constant = constantIn(0, current.format),
                         .type = from};
        return choose(context_, std::span(&zero, 1), from);
    }
    case TermKind::IntegerNegate:
        return value::integerNegate(wanted, std::get<value::IntegerFormat>(current.format)).value;
    case TermKind::IntegerComplement:
        return value::integerComplement(wanted, std::get<value::IntegerFormat>(current.format));
    case TermKind::FloatNegate:
        return value::floatNegate(wanted, std::get<value::FloatFormat>(current.format));
    case TermKind::IntegerBinary: {
        // The other operand keeps its value.
        const std::expected<Value, NoValue> other = terms_.evaluate(current.operands[1 - operand], chosen_);
        if (!other) {
            return std::nullopt;
        }
        const auto format = std::get<value::IntegerFormat>(current.format);
        const auto apply = [&](value::IntegerOperator op, const Value& lhs, const Value& rhs) {
            return value::integerBinary(op, lhs, rhs, format).value; // wraps around, as the run continues
        };
        switch (static_cast<value::IntegerOperator>(current.op)) {
        case value::IntegerOperator::Add:
            return apply(value::IntegerOperator::Sub, wanted, *other);
        case value::IntegerOperator::Sub:
            return operand == 0 ? apply(value::IntegerOperator::Add, wanted, *other)
                                : apply(value::IntegerOperator::Sub, *other, wanted);
        case value::IntegerOperator::Xor:
            return apply(value::IntegerOperator::Xor, wanted, *other);
        case value::IntegerOperator::Mul:
            // Odd factors are invertible modulo 2^bits; signed products may overflow, which inverting cannot undo.
            if (format.isSigned || (other->bits() & 1) == 0) {
                return std::nullopt;
            }
            return apply(value::IntegerOperator::Mul, wanted, Value::integer(inverseOfOdd(other->bits()), format));
        default:
            return std::nullopt;
        }
    }
    default:
        return std::nullopt;
    }
}

// ---- Candidates ----

std::vector<Value> Solver::candidates(InputId input, const Relation& relation) const {
    const SearchInput& info = inputs_[std::to_underlying(input)];
    if (!isArithmetic(context_, info.type)) {
        return {};
    }
    std::vector<Value> result;
    const auto add = [&](std::optional<Value> value) {
        if (value) {
            result.push_back(std::move(*value));
        }
    };
    add(offset(info.value, 1, info.type));
    add(offset(info.value, -1, info.type));
    for (const Int128 number : {0, 1, -1}) {
        add(valueOf(context_, number, info.type));
    }
    // The constants of the relation, the value it compares with first, and their neighbours.
    std::vector<std::pair<Value, ScalarFormat>> constants;
    if (!relation.rhs) {
        constants.emplace_back(relation.constant, relation.format);
    }
    for (const std::optional<TermId> side : {std::optional(relation.lhs), relation.rhs}) {
        if (!side) {
            continue;
        }
        for (const TermId id : terms_.subterms(*side)) {
            const Term& term = terms_.term(id);
            if (term.kind == TermKind::Constant && constants.size() < maxConstants) {
                constants.emplace_back(terms_.constantOf(term), term.format);
            }
        }
    }
    for (const auto& [constant, format] : constants) {
        if (const std::optional<Value> converted = convert(context_, constant, typeOf(context_, format), info.type)) {
            add(converted);
            add(offset(*converted, -1, info.type));
            add(offset(*converted, 1, info.type));
        }
    }
    return result;
}

std::optional<Value> Solver::offset(const Value& value, Int128 delta, ast::QualType type) const {
    if (sema::isInteger(context_, type)) {
        return Value::integer(value.bits() + static_cast<UInt128>(delta), sema::integerFormat(context_, type));
    }
    return value::floatBinary(value::FloatOperator::Add, value, valueOf(context_, delta, type),
                              sema::floatFormat(context_, type));
}

// ---- Inputs and guards ----

std::vector<InputId> Solver::changeable(const std::vector<InputId>& ids) const {
    std::vector<InputId> result;
    for (const InputId id : ids) {
        NITWIT_ASSERT(std::to_underlying(id) < inputs_.size(), "a term has an input the run did not request");
        if (inputs_[std::to_underlying(id)].source != InputSource::Hinted) {
            result.push_back(id);
        }
    }
    // Default inputs before fixed ones, later requests first: the witness has not given those values, and a later
    // input changes less of the path to the waypoint.
    const auto rank = [&](InputId id) {
        return std::pair(inputs_[std::to_underlying(id)].source == InputSource::Default ? 0 : 1,
                         -static_cast<std::int64_t>(std::to_underlying(id)));
    };
    std::ranges::sort(result, {}, rank);
    return result;
}

Solver::Guard Solver::guardOf(const Clause& clause, bool mayBeFalse) const {
    Guard guard{.clause = clause, .inputs = {}, .mayBeFalse = mayBeFalse};
    for (const Atom& atom : clause) {
        if (atom.term) {
            std::ranges::copy(terms_.inputs(*atom.term), std::back_inserter(guard.inputs));
        }
    }
    std::ranges::sort(guard.inputs);
    const auto [first, last] = std::ranges::unique(guard.inputs);
    guard.inputs.erase(first, last);
    return guard;
}

// ---- Text for the log ----

std::string Solver::textOf(TermId id, int depth, bool isTop) const {
    if (depth == 0) {
        return "...";
    }
    const Term& term = terms_.term(id);
    const auto operand = [&](std::size_t i) {
        return i < term.operandCount ? textOf(term.operands[i], depth - 1, false) : std::string("?");
    };
    const auto binary = [&](std::string_view op) {
        const std::string text = std::format("{} {} {}", operand(0), op, operand(1));
        return isTop ? text : "(" + text + ")";
    };
    switch (term.kind) {
    case TermKind::Input:
        return std::format("in{}", std::to_underlying(terms_.inputOf(term)));
    case TermKind::Constant:
        return toString(context_, terms_.constantOf(term), typeOf(context_, term.format));
    case TermKind::Opaque:
        return "<opaque>";
    case TermKind::IntegerBinary:
        return binary(nameOf(static_cast<value::IntegerOperator>(term.op)));
    case TermKind::Shift:
        return binary(static_cast<value::ShiftOperator>(term.op) == value::ShiftOperator::Left ? "<<" : ">>");
    case TermKind::FloatBinary:
        return binary(nameOf(static_cast<value::FloatOperator>(term.op)));
    case TermKind::IntegerCompare:
    case TermKind::FloatCompare:
        return binary(nameOf(static_cast<Comparison>(term.op)));
    case TermKind::LogicalAnd:
        return binary("&&");
    case TermKind::LogicalOr:
        return binary("||");
    case TermKind::IntegerNegate:
    case TermKind::FloatNegate:
        return "-" + operand(0);
    case TermKind::IntegerComplement:
        return "~" + operand(0);
    case TermKind::LogicalNot:
        return "!" + operand(0);
    case TermKind::ToBool:
        return "(_Bool)" + operand(0);
    case TermKind::ConvertInteger:
    case TermKind::IntegerToFloat:
    case TermKind::FloatToInteger:
    case TermKind::ConvertFloat:
        return std::format("({}){}", ast::typeToString(typeOf(context_, term.secondFormat)), operand(0));
    case TermKind::Conditional: {
        const std::string text = term.op == 1 ? std::format("{} ? {} : ?", operand(0), operand(1))
                                              : std::format("{} ? ? : {}", operand(0), operand(1));
        return isTop ? text : "(" + text + ")";
    }
    }
    std::unreachable();
}

// An atom `t != 0` on a truth value is written `t`, and `t == 0` as `!(t)`.
std::string Solver::textOf(const Clause& clause) const {
    std::string text;
    for (const Atom& atom : clause) {
        if (!text.empty()) {
            text += " || ";
        }
        if (!atom.term || terms_.isOpaque(*atom.term)) {
            const ast::QualType type = context_.builtin(ast::BuiltinKind::Int128);
            text += std::format("{} {} {}", atom.term ? "<opaque>" : "<no term>", nameOf(atom.op),
                                toString(context_, valueOf(context_, atom.constant, type), type));
            continue;
        }
        const TermKind kind = terms_.term(*atom.term).kind;
        const bool isTruth = kind == TermKind::IntegerCompare || kind == TermKind::FloatCompare
                             || kind == TermKind::LogicalAnd || kind == TermKind::LogicalOr
                             || kind == TermKind::LogicalNot;
        if (isTruth && atom.constant == 0 && atom.op == Comparison::NotEqual) {
            text += textOf(*atom.term, textDepth, clause.size() == 1);
        } else if (isTruth && atom.constant == 0 && atom.op == Comparison::Equal) {
            text += std::format("!({})", textOf(*atom.term, textDepth, true));
        } else {
            const ScalarFormat format = terms_.resultFormat(*atom.term);
            text += std::format("{} {} {}", textOf(*atom.term, textDepth, false), nameOf(atom.op),
                                toString(context_, constantIn(atom.constant, format), typeOf(context_, format)));
        }
    }
    return text;
}

} // namespace

std::string_view nameOf(Change::Method method) {
    switch (method) {
    case Change::Method::Inversion:
        return "inversion";
    case Change::Method::Candidate:
        return "candidate";
    }
    std::unreachable();
}

SearchResult search(ast::AstContext& context, const value::TermTable& terms, std::span<const Clause> requirement,
                    std::span<const Clause> kept, std::span<const SearchInput> inputs) {
    Solver solver(context, terms, inputs);
    solver.keep(kept);
    return solver.solve(requirement);
}

} // namespace nitwit::validate
