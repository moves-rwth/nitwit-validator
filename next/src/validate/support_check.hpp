#pragma once

// What NITWIT supports of witness format 2.x, checked before any run (validate/README.md, "Structure").

#include <optional>
#include <string>
#include <string_view>

#include "witness/model.hpp"

namespace nitwit::validate {

/// Why NITWIT cannot validate the witness, e.g. "cycle waypoints (non-termination) in segment 2"; nullopt if the
/// witness has exactly one violation sequence of supported structure. Constraint texts are checked when they are
/// compiled, not here.
[[nodiscard]] std::optional<std::string> unsupportedFeature(const witness::YamlWitness& witness);

/// The first waypoint whose location names another file than the program, compared by the last path component, e.g.
/// "a waypoint in 'h.h', not in the program 'prog.c' (segment 2)"; nullopt if there is none. `program` is the path of
/// the original program.
[[nodiscard]] std::optional<std::string> waypointInOtherFile(const witness::ViolationSequence& sequence,
                                                             std::string_view program);

/// Whether NITWIT reads constraints of this format as C expressions: none given, c_expression, and the formats whose
/// constraints sv-witnesses allows to extend C (acsl_expression, ext_c_expression); a constraint of these formats that
/// is not C fails to compile.
[[nodiscard]] bool isSupportedConstraintFormat(const std::string& format);

} // namespace nitwit::validate
