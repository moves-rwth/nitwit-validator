#include "validate/search.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
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

// What one relation tries; SearchLimits bounds the whole search.
constexpr std::size_t maxInputsPerRelation = 8; // the inputs tried for one relation, in the order of preference
constexpr std::size_t maxConstants = 8; // the constants of a relation, and those of clauses blocking values, per input
constexpr int textDepth = 5;            // the levels of a term in the log

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

bool isSameAtom(const Atom& a, const Atom& b) {
    return a.term == b.term && a.op == b.op && a.constant == b.constant;
}

std::uint64_t clauseHash(std::span<const Atom> clause) {
    std::uint64_t hash = 0xcbf29ce484222325ULL; // FNV-1a over the fields of the atoms
    const auto add = [&](std::uint64_t field) {
        hash = (hash ^ field) * 0x100000001b3ULL;
    };
    for (const Atom& atom : clause) {
        add(atom.term ? std::to_underlying(*atom.term) : ~std::uint64_t{0});
        add(std::to_underlying(atom.op));
        add(static_cast<std::uint64_t>(atom.constant));
        add(static_cast<std::uint64_t>(static_cast<UInt128>(atom.constant) >> 64));
    }
    return hash;
}

/// The value of a choice for an input, and how it was found.
struct Choice {
    Value value;
    Change::Method method = Change::Method::Inversion;
};

/// The values to try for one input, each once: the inverted value first, then the candidates, the non-negative ones
/// before the negative ones (programs often reject negative inputs), each group in the order the values came, then the
/// special values; values the input was fixed to in earlier runs come last, or not at all.
class Choices {
public:
    Choices(ast::AstContext& context, const SearchInput& input, bool allowsHistory)
        : context_(context), input_(input), allowsHistory_(allowsHistory) {}

    void add(Value value, Change::Method method) {
        const Queue queue = method == Change::Method::Inversion ? Queue::Inverted
                            : isNegative(value)                 ? Queue::Negative
                                                                : Queue::NonNegative;
        enqueue(std::move(value), method, queue);
    }

    /// Adds a candidate that comes after all other candidates: NaN, an infinity or an extreme of the input's type.
    void addSpecial(Value value) { enqueue(std::move(value), Change::Method::Candidate, Queue::Special); }

    std::optional<Choice> next() {
        for (std::size_t i = 0; i < queues_.size(); ++i) {
            if (next_[i] < queues_[i].size()) {
                return queues_[i][next_[i]++];
            }
        }
        return std::nullopt;
    }

private:
    /// The queues of values, in the order they are tried.
    enum class Queue : std::uint8_t { Inverted, NonNegative, Negative, Special, EarlierRuns };

    void enqueue(Value value, Change::Method method, Queue queue) {
        if (std::ranges::any_of(seen_, [&](const Value& other) { return isSame(other, value); })) {
            return;
        }
        seen_.push_back(value);
        if (std::ranges::any_of(input_.history, [&](const Value& old) { return isSame(old, value); })) {
            if (!allowsHistory_) {
                return;
            }
            queue = Queue::EarlierRuns;
        }
        queues_[std::to_underlying(queue)].push_back({.value = std::move(value), .method = method});
    }

    [[nodiscard]] bool isNegative(const Value& value) const {
        if (sema::isSignedInteger(context_, input_.type)) {
            return value.signedBits() < 0;
        }
        return sema::isFloating(context_, input_.type)
               && value::floatCompare(Comparison::Less, value, valueOf(context_, 0, input_.type),
                                      sema::floatFormat(context_, input_.type));
    }

    ast::AstContext& context_;
    const SearchInput& input_;
    bool allowsHistory_;
    std::array<std::vector<Choice>, 5> queues_; // by Queue
    std::array<std::size_t, 5> next_{};
    std::vector<Value> seen_;
};

/// Solves one requirement greedily over its clauses (validate/README.md, "Search").
class Solver {
public:
    Solver(ast::AstContext& context, const value::TermTable& terms, const KeptClauses& kept, const SearchInputs& inputs,
           SearchLimits limits)
        : context_(context), terms_(terms), kept_(kept), inputs_(inputs), limits_(limits) {}

    SearchResult solve(std::span<const Clause> requirement);

private:
    /// A clause of the requirement, or the part of a goal solved before, that a new value must not make end the run,
    /// nor false unless `mayBeFalse`. Kept clauses are checked through KeptClauses::mentioning.
    struct Guard {
        Clause clause;
        std::vector<InputId> inputs; // the inputs its atoms depend on, ascending
        bool mayBeFalse = false;
    };

    /// `lhs op rhs` in `format`, or its negation; the right side is a term or, without one, `constant`.
    struct Relation {
        TermId lhs{};
        Comparison op = Comparison::Equal;
        std::optional<TermId> rhs;
        Value constant;
        ScalarFormat format;
        Atom atom;              // holds when the relation does: its guard while a kept clause is repaired
        bool isNegated = false; // `!(a < b)`, which NaN satisfies, unlike `a >= b`
    };

    enum class Outcome : std::uint8_t {
        Holds,
        Unknown, // holds as far as the run evaluated the operands
        Fails,
    };

    /// The values chosen so far, to restore after an alternative failed.
    struct Snapshot {
        value::Substitution chosen;
        std::vector<std::pair<InputId, Change::Method>> methods;
    };

    /// One greedy pass over the clauses; whether every clause then holds or is unknown.
    bool solvePass(std::span<const Clause> requirement);

    [[nodiscard]] Truth truthOf(const Atom& atom, const value::Substitution& substitution) const;
    [[nodiscard]] Truth truthOf(std::span<const Atom> clause, const value::Substitution& substitution) const;
    [[nodiscard]] Truth truthOf(const Relation& relation, const value::Substitution& substitution) const;

    Outcome solveAtom(const Atom& atom);
    /// Makes `term` non-zero (`wantTrue`) or zero, decomposing `!`, conversions to _Bool, `&&`, `||`, `?:` and
    /// comparisons. A goal that failed is not decomposed again within one atom.
    Outcome solveGoal(TermId term, bool wantTrue);
    Outcome decompose(TermId term, bool wantTrue);
    /// Solves `c ? x : y` through the operand the run evaluated (`solveSelected`), with the condition keeping its
    /// choice; else makes the condition select the other operand, whose value the next run evaluates (Unknown).
    template <typename SolveSelected>
    Outcome solveConditional(const Term& conditional, SolveSelected solveSelected);
    bool solveRelation(const Relation& relation);
    /// The value of input `input` that gives `side op bound`, through the operations on the path to it.
    [[nodiscard]] std::optional<Value> invert(TermId side, Comparison op, const Value& bound,
                                              const ScalarFormat& format, InputId input) const;
    /// The value an operand of `term` must have so that `term` has the value `wanted`.
    [[nodiscard]] std::optional<Value> preimage(TermId term, std::size_t operand, const Value& wanted) const;
    /// Adds the candidates of `input` for `relation` to `choices`.
    void addCandidates(Choices& choices, const SearchInput& input, const Relation& relation) const;
    /// Adds the special values of the input's type: NaN, the infinities and -0.0, or the extremes and their neighbours.
    void addSpecials(Choices& choices, const SearchInput& input) const;
    /// Adds a constant of `format` and its neighbours, as values of the input's type.
    void addConstant(Choices& choices, const SearchInput& input, const Value& constant,
                     const ScalarFormat& format) const;
    /// Adds the constants of `clause`, at most `budget` of them.
    void addConstantsOf(Choices& choices, const SearchInput& input, std::span<const Atom> clause,
                        std::size_t& budget) const;
    /// Takes `choice` for `input` if it gives the relation and keeps the guards and the kept clauses. Otherwise
    /// `blocker` is the clause the value makes false, if there is one.
    bool tryValue(InputId input, const Choice& choice, const Relation& relation, Clause& blocker);
    /// Makes the kept clause `clause`, which `trial` makes false, hold again through other inputs than `pinned`, whose
    /// value in `trial` gives `relation`. One level: a repair does not repair.
    bool repair(value::Substitution trial, InputId pinned, Change::Method method, std::span<const Atom> clause,
                const Relation& relation);

    /// The inputs the search may change, in the order of preference.
    [[nodiscard]] std::vector<InputId> changeable(std::span<const InputId> ids) const;
    [[nodiscard]] Guard guardOf(std::span<const Atom> clause, bool mayBeFalse) const;
    [[nodiscard]] Value constantIn(Int128 number, const ScalarFormat& format) const {
        return valueOf(context_, number, typeOf(context_, format));
    }
    [[nodiscard]] std::optional<Value> offset(const Value& value, Int128 delta, ast::QualType type) const;
    [[nodiscard]] Snapshot snapshot() const { return {.chosen = chosen_, .methods = methods_}; }
    void restore(Snapshot snapshot) {
        chosen_ = std::move(snapshot.chosen);
        methods_ = std::move(snapshot.methods);
    }
    void setMethod(InputId input, Change::Method method) {
        std::erase_if(methods_, [&](const auto& entry) { return entry.first == input; });
        methods_.emplace_back(input, method);
    }
    /// Pushes a guard; returns what popGuard needs.
    std::size_t pushGuard(Guard guard) {
        guards_.push_back(std::move(guard));
        return failedOrder_.size();
    }
    /// Pops the last guard, and forgets the goals that failed under it: they may hold without it. The memo only saves
    /// cost.
    void popGuard(std::size_t failedBefore) {
        guards_.pop_back();
        for (std::size_t i = failedBefore; i < failedOrder_.size(); ++i) {
            failed_.erase(failedOrder_[i]);
        }
        failedOrder_.resize(failedBefore);
    }
    void forgetFailures() {
        failed_.clear();
        failedOrder_.clear();
    }
    /// A term's value with these inputs, counted against the budget of the search.
    [[nodiscard]] std::expected<Value, NoValue> evaluate(TermId id, const value::Substitution& substitution) const {
        ++evaluations_;
        return terms_.evaluate(id, substitution);
    }
    [[nodiscard]] bool isExhausted() const { return evaluations_ > limits_.evaluations; }

    [[nodiscard]] std::string textOf(TermId id, int depth, bool isTop) const;
    [[nodiscard]] std::string textOf(const Clause& clause) const;

    ast::AstContext& context_;
    const value::TermTable& terms_;
    const KeptClauses& kept_;
    const SearchInputs& inputs_;
    SearchLimits limits_;
    bool allowsHistory_ = false; // whether values inputs were fixed to in earlier runs may be chosen
    value::Substitution chosen_;
    std::vector<std::pair<InputId, Change::Method>> methods_; // how each input in chosen_ got its value
    std::vector<Guard> guards_;
    std::optional<InputId> pinned_; // while a kept clause is repaired: the input whose value broke it
    // The goals that failed for the atom being solved under the guards now, as term * 2 + want, and in the order they
    // failed, so that those that failed under a guard are forgotten with it.
    std::unordered_set<std::uint64_t> failed_;
    std::vector<std::uint64_t> failedOrder_;
    std::size_t goals_ = 0;               // the goals decomposed in this pass
    mutable std::size_t evaluations_ = 0; // of terms in this search, counted by evaluate()
};

// Two passes: the first never chooses a value an input was fixed to in an earlier run, so that two waypoints do not
// take turns changing one input; the second allows them.
SearchResult Solver::solve(std::span<const Clause> requirement) {
    std::string needs;
    for (const Clause& clause : requirement) {
        if (truthOf(clause, {}) != Truth::True) {
            needs += std::format("{}`{}`", needs.empty() ? "" : ", ", textOf(clause));
        }
    }
    if (needs.empty()) {
        // An avoid function_enter that passes has no requirement; a requirement that holds on the run's values is
        // one whose terms do not compute what the program did.
        return {.changes = {},
                .explanation = "needs another path, which the terms do not describe: no input values found"};
    }
    const bool hasHistory = inputs_.hasHistory();
    for (const bool allowsHistory : {false, true}) {
        if (allowsHistory && !hasHistory) {
            break;
        }
        allowsHistory_ = allowsHistory;
        chosen_ = {};
        methods_.clear();
        guards_.clear();
        goals_ = 0;
        if (!solvePass(requirement)) {
            if (isExhausted()) {
                break;
            }
            continue;
        }
        SearchResult result;
        for (const auto& [input, value] : chosen_.entries()) {
            if (isSame(value, inputs_.at(input).value)) {
                continue;
            }
            const auto method = std::ranges::find(methods_, input, &std::pair<InputId, Change::Method>::first)->second;
            result.changes.push_back({.input = input, .value = value, .method = method});
        }
        if (result.changes.empty()) {
            continue;
        }
        result.explanation = std::format("needs {}", needs);
        for (const char* separator = "; "; const Change& change : result.changes) {
            result.explanation +=
                std::format("{}input {} := {} ({})", separator, std::to_underlying(change.input),
                            toString(context_, change.value, inputs_.at(change.input).type), nameOf(change.method));
            separator = ", ";
        }
        return result;
    }
    if (isExhausted()) {
        return {.changes = {},
                .explanation =
                    std::format("needs {}: the search stopped after {} evaluations", needs, limits_.evaluations)};
    }
    return {.changes = {}, .explanation = std::format("needs {}: no input values found", needs)};
}

bool Solver::solvePass(std::span<const Clause> requirement) {
    for (const Clause& clause : requirement) {
        if (isExhausted()) {
            return false;
        }
        if (truthOf(clause, chosen_) != Truth::True) {
            // While the clause is solved, no value may make it end the run.
            const std::size_t failedBefore = pushGuard(guardOf(clause, true));
            bool isSolved = false;
            for (const Atom& atom : clause) {
                Snapshot before = snapshot();
                forgetFailures();
                if (solveAtom(atom) != Outcome::Fails) {
                    const Truth after = truthOf(clause, chosen_);
                    isSolved = after == Truth::True || after == Truth::Unknown;
                }
                if (isSolved) {
                    break;
                }
                restore(std::move(before));
            }
            popGuard(failedBefore);
            if (!isSolved && truthOf(clause, chosen_) != Truth::Unknown) {
                return false;
            }
        }
        guards_.push_back(guardOf(clause, false)); // the values for later clauses keep it
    }
    return true;
}

// ---- Evaluation ----

Truth Solver::truthOf(const Atom& atom, const value::Substitution& substitution) const {
    if (!atom.term || terms_.isOpaque(*atom.term)) {
        return Truth::Unknown;
    }
    const std::expected<Value, NoValue> value = evaluate(*atom.term, substitution);
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
    const std::expected<Value, NoValue> lhs = evaluate(relation.lhs, substitution);
    const std::expected<Value, NoValue> rhs = relation.rhs ? evaluate(*relation.rhs, substitution) : relation.constant;
    if (!lhs || !rhs) {
        const bool endsRun = (!lhs && lhs.error() == NoValue::EndsRun) || (!rhs && rhs.error() == NoValue::EndsRun);
        return endsRun ? Truth::EndsRun : Truth::Unknown;
    }
    return compare(relation.op, *lhs, *rhs, relation.format) != relation.isNegated ? Truth::True : Truth::False;
}

// ---- Decomposition ----

Solver::Outcome Solver::solveAtom(const Atom& atom) {
    if (!atom.term || terms_.isOpaque(*atom.term)) {
        return Outcome::Fails; // unknown: no input is changed for it
    }
    if (atom.constant == 0 && (atom.op == Comparison::Equal || atom.op == Comparison::NotEqual)) {
        return solveGoal(*atom.term, atom.op == Comparison::NotEqual);
    }
    const auto relationOn = [&](TermId term) {
        const ScalarFormat format = terms_.resultFormat(term);
        const Relation relation{.lhs = term,
                                .op = atom.op,
                                .rhs = std::nullopt,
                                .constant = constantIn(atom.constant, format),
                                .format = format,
                                .atom = {.term = term, .op = atom.op, .constant = atom.constant}};
        return solveRelation(relation) ? Outcome::Holds : Outcome::Fails;
    };
    const Term& term = terms_.term(*atom.term);
    if (term.kind == TermKind::Conditional) {
        return solveConditional(term, relationOn);
    }
    return relationOn(*atom.term);
}

Solver::Outcome Solver::solveGoal(TermId term, bool wantTrue) {
    const std::uint64_t key = (std::uint64_t{std::to_underlying(term)} << 1) | (wantTrue ? 1 : 0);
    // Shared subterms (`c = c || c` in a loop) would make the decomposition exponential.
    if (failed_.contains(key) || ++goals_ > limits_.goals || isExhausted()) {
        return Outcome::Fails;
    }
    const Outcome outcome = decompose(term, wantTrue);
    if (outcome == Outcome::Fails && failed_.insert(key).second) {
        failedOrder_.push_back(key);
    }
    return outcome;
}

Solver::Outcome Solver::decompose(TermId term, bool wantTrue) {
    const std::expected<Value, NoValue> now = evaluate(term, chosen_);
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
    case TermKind::Conditional:
        return solveConditional(current, [&](TermId selected) { return solveGoal(selected, wantTrue); });
    case TermKind::IntegerCompare:
    case TermKind::FloatCompare: {
        // A comparison that must fail is solved for its negation, not for the negated operator, which NaN does not
        // satisfy where the negation holds: `!(a < b)` holds with a NaN operand, `a >= b` does not.
        const Relation relation{
            .lhs = current.operands[0],
            .op = static_cast<Comparison>(current.op),
            .rhs = current.operands[1],
            .constant = {},
            .format = current.format,
            .atom = {.term = term, .op = wantTrue ? Comparison::NotEqual : Comparison::Equal, .constant = 0},
            .isNegated = !wantTrue};
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
        const Atom firstHolds{.term = first, .op = wantTrue ? Comparison::NotEqual : Comparison::Equal, .constant = 0};
        const std::size_t failedBefore = pushGuard(guardOf(std::span(&firstHolds, 1), false));
        const Outcome second = solveGoal(current.operands[1], wantTrue);
        popGuard(failedBefore);
        if (second == Outcome::Fails) {
            return second;
        }
        return outcome == Outcome::Holds && second == Outcome::Holds ? Outcome::Holds : Outcome::Unknown;
    }
    default: {
        const ScalarFormat format = terms_.resultFormat(term);
        const Comparison op = wantTrue ? Comparison::NotEqual : Comparison::Equal;
        const Relation relation{.lhs = term,
                                .op = op,
                                .rhs = std::nullopt,
                                .constant = constantIn(0, format),
                                .format = format,
                                .atom = {.term = term, .op = op, .constant = 0}};
        return solveRelation(relation) ? Outcome::Holds : Outcome::Fails;
    }
    }
}

template <typename SolveSelected>
Solver::Outcome Solver::solveConditional(const Term& conditional, SolveSelected solveSelected) {
    const TermId condition = conditional.operands[0];
    const bool selectsFirst = conditional.op == 1;
    Snapshot before = snapshot();
    const Atom keepsChoice{
        .term = condition, .op = selectsFirst ? Comparison::NotEqual : Comparison::Equal, .constant = 0};
    const std::size_t failedBefore = pushGuard(guardOf(std::span(&keepsChoice, 1), false));
    const Outcome outcome = solveSelected(conditional.operands[1]);
    popGuard(failedBefore);
    if (outcome != Outcome::Fails) {
        return outcome;
    }
    restore(std::move(before));
    return solveGoal(condition, !selectsFirst) == Outcome::Fails ? Outcome::Fails : Outcome::Unknown;
}

// ---- Relations ----

// Each input of the relation in the order of preference gets the inverted value, if the relation can be inverted for
// it, and then candidates. A candidate that a clause blocks adds that clause's constants as candidates. The first
// value that gives the relation and keeps the guards and kept clauses is taken.
bool Solver::solveRelation(const Relation& relation) {
    const std::vector<InputId> lhsInputs = terms_.inputs(relation.lhs);
    const std::vector<InputId> rhsInputs = relation.rhs ? terms_.inputs(*relation.rhs) : std::vector<InputId>{};
    std::vector<InputId> all;
    std::ranges::set_union(lhsInputs, rhsInputs, std::back_inserter(all));
    std::vector<InputId> order = changeable(all);
    if (order.size() > maxInputsPerRelation) {
        order.resize(maxInputsPerRelation);
    }
    // The negated operator gives values that satisfy the negation.
    const Comparison op = relation.isNegated ? negated(relation.op) : relation.op;
    for (const InputId input : order) {
        const SearchInput info = inputs_.at(input);
        Choices choices(context_, info, allowsHistory_);
        // Inversion needs the input on one side only; the other side is the bound.
        const bool isLeft = std::ranges::binary_search(lhsInputs, input);
        if (isLeft != std::ranges::binary_search(rhsInputs, input)) {
            const TermId side = isLeft ? relation.lhs : *relation.rhs;
            const std::expected<Value, NoValue> bound = !isLeft        ? evaluate(relation.lhs, chosen_)
                                                        : relation.rhs ? evaluate(*relation.rhs, chosen_)
                                                                       : relation.constant;
            if (bound) {
                if (std::optional<Value> inverted =
                        invert(side, isLeft ? op : mirrored(op), *bound, relation.format, input)) {
                    choices.add(std::move(*inverted), Change::Method::Inversion);
                }
            }
        }
        addCandidates(choices, info, relation);
        std::size_t blockerConstants = maxConstants;
        Clause blocker;
        while (const std::optional<Choice> choice = choices.next()) {
            blocker.clear();
            if (tryValue(input, *choice, relation, blocker)) {
                return true;
            }
            addConstantsOf(choices, info, blocker, blockerConstants);
        }
    }
    return false;
}

bool Solver::tryValue(InputId input, const Choice& choice, const Relation& relation, Clause& blocker) {
    if (isExhausted()) {
        return false;
    }
    const SearchInput info = inputs_.at(input);
    // A value of the input's type: a _Bool is 0 or 1.
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
        if (truth == Truth::EndsRun) {
            return false;
        }
        if (truth == Truth::False && !guard.mayBeFalse) {
            blocker = guard.clause;
            return false;
        }
    }
    std::optional<std::uint32_t> broken; // the one kept clause the value makes false
    for (const std::uint32_t index : kept_.mentioning(input)) {
        const Truth truth = truthOf(kept_[index], trial);
        if (truth == Truth::EndsRun) {
            return false;
        }
        if (truth == Truth::False) {
            if (broken) {
                blocker.assign(kept_[index].begin(), kept_[index].end());
                return false;
            }
            broken = index;
        }
    }
    if (broken) {
        blocker.assign(kept_[*broken].begin(), kept_[*broken].end());
        return !pinned_ && repair(std::move(trial), input, choice.method, kept_[*broken], relation);
    }
    if (isExhausted()) {
        return false;
    }
    chosen_ = std::move(trial);
    setMethod(input, choice.method);
    return true;
}

// validate/README.md, "Search": the input whose value broke the kept clause keeps it, and the relation it gives must
// keep holding; the other inputs of the clause may change, as far as their own guards and kept clauses allow.
bool Solver::repair(value::Substitution trial, InputId pinned, Change::Method method, std::span<const Atom> clause,
                    const Relation& relation) {
    Snapshot before = snapshot();
    chosen_ = std::move(trial);
    setMethod(pinned, method);
    pinned_ = pinned;
    // The goals that failed outside the repair, with other values, may hold in it, and the other way round.
    std::unordered_set<std::uint64_t> outerFailed = std::exchange(failed_, {});
    std::vector<std::uint64_t> outerFailedOrder = std::exchange(failedOrder_, {});
    const std::size_t failedBefore = pushGuard(guardOf(std::span(&relation.atom, 1), false));
    bool isRepaired = false;
    for (const Atom& atom : clause) {
        Snapshot attempt = snapshot();
        forgetFailures();
        if (solveAtom(atom) != Outcome::Fails && !isExhausted()) {
            const Truth truth = truthOf(clause, chosen_);
            isRepaired = (truth == Truth::True || truth == Truth::Unknown) && truthOf(relation, chosen_) == Truth::True;
        }
        if (isRepaired) {
            break;
        }
        restore(std::move(attempt));
    }
    popGuard(failedBefore);
    failed_ = std::move(outerFailed);
    failedOrder_ = std::move(outerFailedOrder);
    pinned_.reset();
    if (!isRepaired) {
        restore(std::move(before));
    }
    return isRepaired;
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

// Each case gives the value that makes the operation give `wanted` exactly, or none. Where rounding or truncation
// may make it inexact, the evaluation of the relation in tryValue decides.
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
    default:
        break;
    }

    // Binary operations: the other operand keeps its value.
    if (current.kind != TermKind::IntegerBinary && current.kind != TermKind::FloatBinary) {
        return std::nullopt;
    }
    const std::expected<Value, NoValue> other = evaluate(current.operands[1 - operand], chosen_);
    if (!other) {
        return std::nullopt;
    }
    if (current.kind == TermKind::FloatBinary) {
        const auto format = std::get<value::FloatFormat>(current.format);
        switch (static_cast<value::FloatOperator>(current.op)) {
        case value::FloatOperator::Add:
            return value::floatBinary(value::FloatOperator::Sub, wanted, *other, format);
        case value::FloatOperator::Sub:
            return operand == 0 ? value::floatBinary(value::FloatOperator::Add, wanted, *other, format)
                                : value::floatBinary(value::FloatOperator::Sub, *other, wanted, format);
        default:
            return std::nullopt;
        }
    }
    const auto format = std::get<value::IntegerFormat>(current.format);
    // Defined results only, except for the wrap-around of + and -, which the run continues with.
    const auto exact = [&](value::IntegerOperator op, const Value& lhs, const Value& rhs) -> std::optional<Value> {
        const value::Result result = value::integerBinary(op, lhs, rhs, format);
        return result.undefinedBehavior == value::UndefinedBehavior::None ? std::optional(result.value) : std::nullopt;
    };
    const auto wrapped = [&](value::IntegerOperator op, const Value& lhs, const Value& rhs) {
        return value::integerBinary(op, lhs, rhs, format).value;
    };
    switch (static_cast<value::IntegerOperator>(current.op)) {
    case value::IntegerOperator::Add:
        return wrapped(value::IntegerOperator::Sub, wanted, *other);
    case value::IntegerOperator::Sub:
        return operand == 0 ? wrapped(value::IntegerOperator::Add, wanted, *other)
                            : wrapped(value::IntegerOperator::Sub, *other, wanted);
    case value::IntegerOperator::Xor:
        return wrapped(value::IntegerOperator::Xor, wanted, *other);
    case value::IntegerOperator::Mul: {
        // A factor that divides the wanted value gives the quotient. In an unsigned format, a factor 2^k * odd
        // gives (wanted / 2^k) * odd^-1 modulo 2^bits where wanted has k trailing zero bits.
        if (other->bits() == 0) {
            return std::nullopt;
        }
        const std::optional<Value> remainder = exact(value::IntegerOperator::Rem, wanted, *other);
        if (remainder && remainder->bits() == 0) {
            return exact(value::IntegerOperator::Div, wanted, *other);
        }
        if (format.isSigned) {
            return std::nullopt;
        }
        UInt128 odd = other->bits();
        unsigned twos = 0;
        for (; (odd & 1) == 0; odd >>= 1) {
            ++twos;
        }
        if ((wanted.bits() & ((UInt128{1} << twos) - 1)) != 0) {
            return std::nullopt;
        }
        return Value::integer((wanted.bits() >> twos) * inverseOfOdd(odd), format);
    }
    case value::IntegerOperator::Div:
        // x / c == w for x = w * c, unless the product overflows.
        if (operand != 0 || other->bits() == 0) {
            return std::nullopt;
        }
        return exact(value::IntegerOperator::Mul, wanted, *other);
    default:
        return std::nullopt;
    }
}

// ---- Candidates ----

void Solver::addCandidates(Choices& choices, const SearchInput& input, const Relation& relation) const {
    if (!isArithmetic(context_, input.type)) {
        return;
    }
    const auto add = [&](std::optional<Value> value) {
        if (value) {
            choices.add(std::move(*value), Change::Method::Candidate);
        }
    };
    add(offset(input.value, 1, input.type));
    add(offset(input.value, -1, input.type));
    for (const Int128 number : {0, 1, -1}) {
        add(valueOf(context_, number, input.type));
    }
    // The constants of the relation, the value it compares with first.
    std::size_t budget = maxConstants;
    if (!relation.rhs) {
        addConstant(choices, input, relation.constant, relation.format);
        --budget;
    }
    for (const std::optional<TermId> side : {std::optional(relation.lhs), relation.rhs}) {
        if (!side) {
            continue;
        }
        for (const TermId id : terms_.subterms(*side)) {
            const Term& term = terms_.term(id);
            if (term.kind == TermKind::Constant && budget > 0) {
                addConstant(choices, input, terms_.constantOf(term), term.format);
                --budget;
            }
        }
    }
    addSpecials(choices, input);
}

// They satisfy relations that hold for no other value, such as `x != x` (NaN), `x + 1 > x` false (the maximum, where +
// wraps around) or `x < x + 1e-6` false (an infinity); they come after the other candidates, where those do not help.
void Solver::addSpecials(Choices& choices, const SearchInput& input) const {
    if (sema::isInteger(context_, input.type)) {
        // The non-negative ones first, as for the other candidates.
        const value::IntegerFormat format = sema::integerFormat(context_, input.type);
        const Value minimum = Value::integer(format.isSigned ? UInt128{1} << (format.bits - 1) : 0, format);
        const Value maximum = Value::integer(minimum.bits() - 1, format); // modulo 2^bits
        choices.addSpecial(maximum);
        choices.addSpecial(*offset(maximum, -1, input.type));
        choices.addSpecial(minimum);
        choices.addSpecial(*offset(minimum, 1, input.type));
        return;
    }
    const value::FloatFormat format = sema::floatFormat(context_, input.type);
    constexpr double infinity = std::numeric_limits<double>::infinity();
    for (const double special : {std::numeric_limits<double>::quiet_NaN(), infinity, -infinity, -0.0}) {
        choices.addSpecial(value::convertFloat(Value::ofDouble(special), value::FloatFormat::Double, format));
    }
}

void Solver::addConstant(Choices& choices, const SearchInput& input, const Value& constant,
                         const ScalarFormat& format) const {
    const std::optional<Value> converted = convert(context_, constant, typeOf(context_, format), input.type);
    if (!converted) {
        return;
    }
    for (const Int128 delta : {0, -1, 1}) {
        if (std::optional<Value> value = offset(*converted, delta, input.type)) {
            choices.add(std::move(*value), Change::Method::Candidate);
        }
    }
}

void Solver::addConstantsOf(Choices& choices, const SearchInput& input, std::span<const Atom> clause,
                            std::size_t& budget) const {
    if (!isArithmetic(context_, input.type)) {
        return;
    }
    for (const Atom& atom : clause) {
        if (!atom.term || terms_.isOpaque(*atom.term) || budget == 0) {
            continue;
        }
        const ScalarFormat format = terms_.resultFormat(*atom.term);
        addConstant(choices, input, constantIn(atom.constant, format), format);
        --budget;
        for (const TermId id : terms_.subterms(*atom.term)) {
            const Term& term = terms_.term(id);
            if (term.kind == TermKind::Constant && budget > 0) {
                addConstant(choices, input, terms_.constantOf(term), term.format);
                --budget;
            }
        }
    }
}

std::optional<Value> Solver::offset(const Value& value, Int128 delta, ast::QualType type) const {
    if (sema::isInteger(context_, type)) {
        return Value::integer(value.bits() + static_cast<UInt128>(delta), sema::integerFormat(context_, type));
    }
    return value::floatBinary(value::FloatOperator::Add, value, valueOf(context_, delta, type),
                              sema::floatFormat(context_, type));
}

// ---- Inputs and guards ----

std::vector<InputId> Solver::changeable(std::span<const InputId> ids) const {
    std::vector<InputId> result;
    for (const InputId id : ids) {
        // The search reaches inputs through terms only, and so through this function.
        NITWIT_ASSERT(std::to_underlying(id) < inputs_.size(),
                      "a term has an input the run did not request, or one the validator did not record");
        const SearchInput input = inputs_.at(id);
        // Hinted inputs have the witness's values; pointers are not chosen.
        if (input.source != InputSource::Hinted && isArithmetic(context_, input.type) && id != pinned_) {
            result.push_back(id);
        }
    }
    // Default inputs before fixed ones, later requests first: the witness has not given those values, and a later
    // input changes less of the path to the waypoint.
    const auto rank = [&](InputId id) {
        return std::pair(inputs_.at(id).source == InputSource::Default ? 0 : 1,
                         -static_cast<std::int64_t>(std::to_underlying(id)));
    };
    std::ranges::sort(result, {}, rank);
    return result;
}

Solver::Guard Solver::guardOf(std::span<const Atom> clause, bool mayBeFalse) const {
    Guard guard{.clause = Clause(clause.begin(), clause.end()), .inputs = {}, .mayBeFalse = mayBeFalse};
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

// An atom `t != 0` on a truth value is written `t`, and `t == 0` as `!(t)`. A clause without atoms, which no value
// satisfies (an avoided default of a switch without case labels), is `false`.
std::string Solver::textOf(const Clause& clause) const {
    if (clause.empty()) {
        return "false";
    }
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

// ---- KeptClauses ----

KeptClauses::Outcome KeptClauses::keep(const value::TermTable& terms, std::span<const Atom> clause) {
    const auto hasInputs = [&](const Atom& atom) {
        return atom.term && !terms.isOpaque(*atom.term);
    };
    if (std::ranges::none_of(clause, hasInputs)) {
        return Outcome::Ignored;
    }
    const std::uint64_t hash = clauseHash(clause);
    for (auto [found, end] = byHash_.equal_range(hash); found != end; ++found) {
        if (std::ranges::equal((*this)[found->second], clause, isSameAtom)) {
            return Outcome::Ignored;
        }
    }
    if (atoms_.size() + clause.size() > atomLimit_) {
        return Outcome::Full;
    }
    const auto index = static_cast<std::uint32_t>(size());
    starts_.push_back(static_cast<std::uint32_t>(atoms_.size()));
    atoms_.insert(atoms_.end(), clause.begin(), clause.end());
    byHash_.emplace(hash, index);
    std::vector<InputId> inputs;
    for (const Atom& atom : clause) {
        if (hasInputs(atom)) {
            std::ranges::copy(terms.inputs(*atom.term), std::back_inserter(inputs));
        }
    }
    std::ranges::sort(inputs);
    const auto [first, last] = std::ranges::unique(inputs);
    inputs.erase(first, last);
    for (const InputId input : inputs) {
        const std::size_t id = std::to_underlying(input);
        if (byInput_.size() <= id) {
            byInput_.resize(id + 1);
        }
        byInput_[id].push_back(index);
    }
    return Outcome::Kept;
}

void KeptClauses::clear() {
    atoms_.clear();
    starts_.clear();
    byInput_.clear();
    byHash_.clear();
}

std::span<const Atom> KeptClauses::operator[](std::size_t index) const {
    NITWIT_ASSERT(index < starts_.size(), "no kept clause with this index");
    const std::size_t end = index + 1 < starts_.size() ? starts_[index + 1] : atoms_.size();
    return std::span(atoms_).subspan(starts_[index], end - starts_[index]);
}

std::span<const std::uint32_t> KeptClauses::mentioning(InputId input) const {
    const std::size_t id = std::to_underlying(input);
    return id < byInput_.size() ? std::span<const std::uint32_t>(byInput_[id]) : std::span<const std::uint32_t>();
}

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
                    const KeptClauses& kept, const SearchInputs& inputs, SearchLimits limits) {
    return Solver(context, terms, kept, inputs, limits).solve(requirement);
}

SearchResult search(ast::AstContext& context, const value::TermTable& terms, std::span<const Clause> requirement,
                    const KeptClauses& kept, std::span<const SearchInput> inputs, SearchLimits limits) {
    class SpanInputs final : public SearchInputs {
    public:
        explicit SpanInputs(std::span<const SearchInput> inputs) : inputs_(inputs) {}
        [[nodiscard]] std::size_t size() const override { return inputs_.size(); }
        [[nodiscard]] SearchInput at(InputId id) const override { return inputs_[std::to_underlying(id)]; }
        [[nodiscard]] bool hasHistory() const override {
            return std::ranges::any_of(inputs_, [](const SearchInput& input) { return !input.history.empty(); });
        }

    private:
        std::span<const SearchInput> inputs_;
    };
    return search(context, terms, requirement, kept, SpanInputs(inputs), limits);
}

} // namespace nitwit::validate
