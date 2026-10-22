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

    /// Forgets the requests and the hint of the previous run; fixed inputs stay.
    void beginRun();

    /// Answers the next request made at `call` with `value`, a value of the call's type (validate/README.md,
    /// "Values": hints of function_return waypoints). A later hint replaces an unused one.
    void hint(const ast::CallExpr* call, value::Value value);
    void clearHint() { hint_.reset(); }

    /// Answers the request with this key with `value` in later runs, until it is fixed to another value.
    void fix(InputKey key, value::Value value);
    /// The values the request with this key was fixed to, oldest first; the last one is its value now.
    [[nodiscard]] std::span<const value::Value> fixedValues(InputKey key) const;

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

    ast::AstContext& context_;
    Int128 defaultValue_;
    // By key, the values it was fixed to. Only looked up, never iterated: the order is by address.
    std::map<InputKey, std::vector<value::Value>, KeyLess> fixed_;
    std::vector<ProvidedInput> inputs_; // of the current run, indexed by InputId
    std::optional<Hint> hint_;
};

} // namespace nitwit::validate
