#pragma once

// Witness constraints compiled against the program: C expressions parsed and analyzed in the scope of the event where
// they are evaluated and split into conjuncts, and the expectations of branching waypoints (validate/README.md,
// "Evaluation points" and "Values").

#include <compare>
#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "support/diagnostics.hpp"
#include "support/int128.hpp"
#include "validate/validator.hpp"
#include "value/arithmetic.hpp"

namespace nitwit::validate {

/// A waypoint of the violation sequence: its segment and its position in the segment, both counted from 0.
struct WaypointId {
    std::uint32_t segment = 0;
    std::uint32_t index = 0;

    friend auto operator<=>(const WaypointId&, const WaypointId&) = default;
};

/// One operand of a constraint's top-level `&&` (validate/README.md: "`&&`-conjunctions are split").
struct Conjunct {
    const ast::Expr* expr = nullptr; // an analyzed scalar rvalue
    /// Set if the conjunct is `subject op constant`, also when written `constant op subject`: a relation that choosing
    /// the subject's value can make true. The subject is without the implicit conversions to the comparison's type
    /// (promotions, usual arithmetic conversions), which can lose the origin of an input although they keep its
    /// value (`unsigned short` to `int` to `unsigned int`). The constant has the comparison's type.
    const ast::Expr* subject = nullptr;
    const ast::Expr* constant = nullptr;
    value::Comparison op = value::Comparison::Equal;
    ast::QualType type; // of the comparison
};

struct CompiledConstraint {
    std::vector<Conjunct> conjuncts;
};

/// What a branching waypoint expects (witness format 2.x, "branching"): the outcome of a condition, or for a `switch`
/// the value of a case label or the default branch.
struct BranchExpectation {
    enum class Kind : std::uint8_t { True, False, Case, Default };

    Kind kind = Kind::True;
    Int128 caseValue = 0; // for Case
};

/// Compiles constraints on first use and keeps them for later runs. Constraints are compiled per waypoint and event
/// location, since the scope and the type of `\result` depend on the location. Keeping them also keeps the sites of
/// the input requests made while evaluating them (reads of uninitialized variables) the same in every run.
class ConstraintCompiler {
public:
    ConstraintCompiler(const ProgramInput& program, DiagnosticEngine& diagnostics);

    /// The C expression `text` compiled for evaluation at `location` in the body of `function` (null for file scope
    /// only), with `\result` of type `resultType` (null where `\result` is not allowed). An error is reported to the
    /// diagnostics and returned as its formatted text.
    [[nodiscard]] std::expected<const CompiledConstraint*, std::string>
    compile(WaypointId waypoint, std::string_view text, const ast::FunctionDecl* function, SourceLocation location,
            ast::QualType resultType);

    /// The expectation a branching waypoint's constraint states: "true", "false", "default", or a case value, which
    /// is an integer constant expression in the scope at `location`.
    [[nodiscard]] std::expected<BranchExpectation, std::string>
    branch(WaypointId waypoint, std::string_view text, const ast::FunctionDecl* function, SourceLocation location);

private:
    using Key = std::pair<WaypointId, std::uint32_t>; // the waypoint and the offset of the event location

    /// Parses and analyzes `text`; null after reporting an error.
    ast::Expr* analyze(std::string_view text, const ast::FunctionDecl* function, SourceLocation location,
                       ast::QualType resultType);
    /// The first error reported since `first` diagnostics, formatted.
    [[nodiscard]] std::string errorSince(std::size_t first) const;

    const ProgramInput& program_;
    DiagnosticEngine& diagnostics_;
    std::map<Key, CompiledConstraint> constraints_;
    std::map<Key, BranchExpectation> branches_;
};

} // namespace nitwit::validate
