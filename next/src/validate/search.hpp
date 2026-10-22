#pragma once

// The search for input values (validate/README.md, "Search"): what a waypoint evaluation needs is a requirement over
// the terms of the run, and the search chooses inputs for the next run that satisfy it and keep the outcomes the run
// already had. It never runs the program; the next run checks every prediction.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ast/context.hpp"
#include "ast/type.hpp"
#include "support/int128.hpp"
#include "validate/oracle.hpp"
#include "value/arithmetic.hpp"
#include "value/term.hpp"
#include "value/value.hpp"

namespace nitwit::validate {

/// `term op constant`, compared in the format of the term's value, to which the integer constant is converted. An atom
/// without term (its value depends on no input the search can change, or it has no value) or on the opaque term is
/// unknown.
struct Atom {
    std::optional<value::TermId> term;
    value::Comparison op = value::Comparison::NotEqual;
    Int128 constant = 0;
};

/// Holds if one of its atoms holds. A requirement, and the kept clauses of a run, are conjunctions of clauses.
using Clause = std::vector<Atom>;

/// The clauses of the waypoints that had their outcome in a run ("kept clauses"): each clause once, and indexed by the
/// inputs it depends on, so that the search checks a new value of an input against the clauses that mention it only.
class KeptClauses {
public:
    /// The default limit: a kept clause takes about 100 bytes, and the search evaluates the clauses of an input for
    /// each value it tries.
    static constexpr std::size_t defaultLimit = 100'000;

    enum class Outcome : std::uint8_t {
        Kept,
        Ignored, // kept already, or no atom on a term other than the opaque term: no input can make it false
        Full,    // the limit is reached
    };

    explicit KeptClauses(std::size_t limit = defaultLimit) : limit_(limit) {}

    /// Keeps a clause whose terms are in `terms`, the table of the run.
    Outcome keep(const value::TermTable& terms, std::span<const Atom> clause);
    void clear();
    [[nodiscard]] std::size_t size() const { return starts_.size(); }
    [[nodiscard]] std::span<const Atom> operator[](std::size_t index) const;
    /// The indices of the clauses that depend on `input`, ascending.
    [[nodiscard]] std::span<const std::uint32_t> mentioning(value::InputId input) const;

private:
    std::size_t limit_;
    std::vector<Atom> atoms_;
    std::vector<std::uint32_t> starts_;                            // the index of each clause's first atom
    std::vector<std::vector<std::uint32_t>> byInput_;              // by InputId: the clauses that mention it
    std::unordered_multimap<std::uint64_t, std::uint32_t> byHash_; // to find a clause kept already
};

/// An input of the run, as the search sees it.
struct SearchInput {
    value::Value value; // in the run
    ast::QualType type;
    InputSource source = InputSource::Default; // the search changes Default and Fixed inputs, not Hinted ones
    std::span<const value::Value> history;     // the values the input was fixed to in earlier runs
};

/// A new value of an input, and how the search found it.
struct Change {
    enum class Method : std::uint8_t { Inversion, Candidate };

    value::InputId input{};
    value::Value value; // of the input's type
    Method method = Method::Inversion;
};

struct SearchResult {
    std::vector<Change> changes; // none if the search found no new values
    std::string explanation;     // for the log: what the requirement needs, and the values chosen for it
};

/// New values of inputs with which every clause of `requirement` holds or is unknown, while no clause of `kept` becomes
/// false or ends the run. `inputs` are the inputs of the run, indexed by InputId; `terms` must be the table of the run.
[[nodiscard]] SearchResult search(ast::AstContext& context, const value::TermTable& terms,
                                  std::span<const Clause> requirement, const KeptClauses& kept,
                                  std::span<const SearchInput> inputs);

/// "inversion" or "candidate", for the log.
[[nodiscard]] std::string_view nameOf(Change::Method method);

} // namespace nitwit::validate
