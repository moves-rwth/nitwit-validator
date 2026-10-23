#pragma once

// Validation mode (driver/README.md): validates a violation witness against a program and prints the diagnostics and
// one result line; the exit code is the verdict's (docs/exit-codes.md).

#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include "support/target.hpp"

namespace nitwit::driver {

struct ValidationCommand {
    std::string witness;
    std::string program;
    std::string propertyFile;  // empty if not given
    std::string errorFunction; // empty if not given; then from the property file, else reach_error
    DataModel dataModel = DataModel::ILP32;
    bool verbose = false;
};

/// The error function of an unreach-call property, e.g. "reach_error" for
/// `CHECK( init(main()), LTL(G ! call(reach_error())) )`; nullopt for any other property.
[[nodiscard]] std::optional<std::string> errorFunctionOf(std::string_view property);

/// Runs the steps of driver/README.md. Diagnostics and the result line go to `out`, the validation log (with
/// `verbose`) to `log`. Returns the exit code. Call it on a large stack.
[[nodiscard]] int validateWitness(const ValidationCommand& command, std::ostream& out, std::ostream& log);

} // namespace nitwit::driver
