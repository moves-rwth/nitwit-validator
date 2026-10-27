#pragma once

// Concrete witnesses (validate/README.md, "Concrete witnesses"): the run that confirmed a witness, written as a witness
// in format 2.2 that gives every input of the run by a function_return waypoint, so that a validation with strict
// inputs (ValidationOptions::strictInputs) follows it in one run.

#include <cstddef>
#include <expected>
#include <optional>
#include <string>

#include "ast/context.hpp"
#include "ast/type.hpp"
#include "validate/validator.hpp"
#include "value/value.hpp"

namespace nitwit::validate {

/// What a concrete witness states besides the run; the caller knows it.
struct ConcreteTask {
    /// The program as the validation got it: the witness's input file and the file of every waypoint.
    std::string programPath;
    /// The property, e.g. "CHECK( init(main()), LTL(G ! call(reach_error())) )".
    std::string specification;
    std::string producerVersion;
    /// ISO 8601, e.g. "2026-10-27T12:00:00Z": the only part of the witness that depends on more than the validation.
    std::string creationTime;
    /// The most inputs a concrete witness gives: each takes a segment of about 200 bytes plus the program's path, and a
    /// constraint that the check of the witness compiles. A million covers the longest witnesses of the SV-COMP 2026
    /// corpus (600,001 segments).
    std::size_t maxInputs = 1'000'000;
    /// Whether to check the witness before it is written: read it back and validate it with strict inputs, which
    /// repeats the confirming run.
    bool check = true;
};

struct ConcreteWitness {
    std::string text; // YAML, format 2.2
    std::size_t inputs = 0;
    /// The first undefined behaviour in the program that the run continued after, e.g. "signed integer overflow at
    /// 7:12"; empty if there was none. There the witness follows what GCC-compiled code does on x86, which the C
    /// standard leaves open.
    std::string undefinedBehavior;
};

/// The concrete witness of `run`, the confirming run of a validation of `program` with `options`, if a validation of
/// it with the same options and strict inputs confirms it (with `task.check`); otherwise why there is none: an input
/// that is no call's result (a read of uninitialized memory, the bytes of __VERIFIER_nondet_memory), a value that
/// resultConstraint() cannot give, a location that a witness cannot give (after a change by the preprocessor), more
/// inputs than `task.maxInputs` or than the oracle recorded.
[[nodiscard]] std::expected<ConcreteWitness, std::string> writeConcreteWitness(const ConfirmingRun& run,
                                                                               const ProgramInput& program,
                                                                               const ConcreteTask& task,
                                                                               const ValidationOptions& options);

/// The constraint of a function_return waypoint (an ext_c_expression) that gives exactly `value` of the scalar type
/// `type`: `\result == c` with an integer constant (suffix U for unsigned types) or a hexadecimal floating constant
/// (suffix f or L by the type), `\result == 0` for a null pointer, `\result > MAX` for +∞ and `\result < -MAX` for −∞
/// (MAX the largest finite value of the type), `\result != \result` for NaN. Nullopt where no constant gives the
/// value: an integer beyond 64 bits, a pointer other than null.
[[nodiscard]] std::optional<std::string> resultConstraint(ast::AstContext& context, const value::Value& value,
                                                          ast::QualType type);

} // namespace nitwit::validate
