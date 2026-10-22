#pragma once

// The input oracle of a validation (architecture.md, "Validation"): it answers each request with the value fixed by an
// earlier run, else with the hint of a function_return waypoint for this call, else with a default.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "ast/context.hpp"
#include "ast/expr.hpp"
#include "interp/oracle.hpp"
#include "support/int128.hpp"
#include "value/value.hpp"

namespace nitwit::validate {

/// Identifies a request across runs: its site and how many requests the site made before in the run. Runs are
/// deterministic, so the same key denotes the same request until an earlier input changes.
struct InputKey {
    const ast::Expr* site = nullptr;
    std::uint32_t occurrence = 0;
};

enum class InputSource : std::uint8_t { Fixed, Hinted, Default };

/// A request of the current run and how it was answered.
struct ProvidedInput {
    InputKey key;
    ast::QualType type;
    InputSource source = InputSource::Default;
    const ast::FunctionDecl* function = nullptr; // the called function, for calls
    value::Value value;                          // the answer
};

class WitnessOracle final : public interp::InputOracle {
public:
    /// `defaultValue` is the value of inputs that are neither fixed nor hinted, converted to their type.
    explicit WitnessOracle(ast::AstContext& context, Int128 defaultValue = 0);

    /// Forgets the requests and the hint of the previous run; fixed inputs stay. Runs are numbered from 1.
    void beginRun();

    /// Answers the next request made at `call` with `value`, a value of the call's type (validate/README.md,
    /// "Values": hints of function_return waypoints). A later hint replaces an unused one.
    void hint(const ast::CallExpr* call, value::Value value);
    void clearHint() { hint_.reset(); }

    /// Answers the request with this key with `value` in later runs, until it is fixed to another value.
    void fix(InputKey key, value::Value value);
    /// The values the request with this key was fixed to, each once, in the order they were first fixed.
    [[nodiscard]] std::span<const value::Value> fixedValues(InputKey key) const;
    /// The earlier run whose fixed inputs had the values they have now, with no value fixed since that had not been
    /// fixed before it. The next run would repeat it, and the search after it, which depends on the run and the values
    /// fixed before, would propose what it proposed then (validate/README.md, "Search").
    [[nodiscard]] std::optional<std::uint32_t> repeatedRun() const;

    value::Value provide(const interp::InputRequest& request) override;

    /// A request of the current run; precondition: the run made it.
    [[nodiscard]] const ProvidedInput& input(value::InputId id) const;
    /// The number of requests of the current run.
    [[nodiscard]] std::size_t inputCount() const { return inputs_.size(); }

private:
    struct KeyLess {
        bool operator()(const InputKey& a, const InputKey& b) const {
            if (a.site != b.site) {
                return std::less<const ast::Expr*>{}(a.site, b.site);
            }
            return a.occurrence < b.occurrence;
        }
    };
    struct Hint {
        const ast::CallExpr* call = nullptr;
        value::Value value;
    };
    /// The values a key was fixed to: each fix with the first run it answered, and each value once.
    struct History {
        std::vector<value::Value> values;
        std::vector<std::uint32_t> runs;
        std::vector<value::Value> distinct;
    };
    /// What a run starts with besides its inputs: a fingerprint of the fixed values, the sum of their hashes, which
    /// does not depend on the order of the keys and is updated in constant time (runs with the same fingerprint are
    /// compared exactly); and the number of distinct values the keys were fixed to.
    struct RunStart {
        std::uint64_t fingerprint = 0;
        std::size_t distinctValues = 0;
    };

    ast::AstContext& context_;
    Int128 defaultValue_;
    // Iterated only where the order does not matter: it is by address.
    std::map<InputKey, History, KeyLess> fixed_;
    std::uint32_t run_ = 0;
    RunStart next_;                     // of the run after the current one, as far as values are fixed now
    std::vector<RunStart> runStarts_;   // by run - 1
    std::vector<ProvidedInput> inputs_; // of the current run, indexed by InputId
    std::optional<Hint> hint_;
};

} // namespace nitwit::validate
