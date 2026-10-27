#pragma once

// Executes a typed AST. buildProgram lowers the function bodies once; each run then starts from scratch with fresh
// memory, so that the validation layer can restart with other inputs.

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ostream>
#include <string>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "interp/observer.hpp"
#include "interp/oracle.hpp"
#include "support/error.hpp"
#include "support/source_manager.hpp"

namespace nitwit::interp {

/// The lowered program: an instruction list per function and the plan for globals and string literals. Immutable
/// after building, so runs cannot influence each other.
class Program;

/// Lowers all function definitions of an analyzed translation unit. Fails if `main` is missing or a construct cannot
/// be lowered (computed goto, ...).
[[nodiscard]] std::expected<std::shared_ptr<const Program>, Error> buildProgram(const ast::TranslationUnit& unit,
                                                                                ast::AstContext& context);

/// A check of the terms of a run (RunOptions::checkTerms): what is wrong with `term` as the term of `value`, which the
/// run computed; nullopt if nothing is.
using TermCheck = std::optional<std::string> (*)(const value::TermTable& terms, value::TermId term,
                                                 const value::Value& value);

/// The consistency check of the terms (terms.cpp): `term` must evaluate to exactly `value` with the run's inputs,
/// unless it is the opaque term.
[[nodiscard]] std::optional<std::string> termMismatch(const value::TermTable& terms, value::TermId term,
                                                      const value::Value& value);

struct RunOptions {
    std::uint64_t maxSteps = 0;             // statements, conditions and calls executed; 0 = unlimited
    std::uint32_t maxCallDepth = 100000;    // nested calls
    std::uint64_t memoryLimit = 4ull << 30; // bytes of live heap and stack objects
    std::ostream* output = nullptr;         // where printf and puts write; null discards the output
    value::TermLimits terms{};              // maxTerms 0 records no terms (replay)
    /// For testing the recording of terms: termMismatch, or null for no check. The run applies it to every term it
    /// records for an operation or reads from memory, also in a constraint, and ends (Unsupported, "internal error:
    /// ...") at the first mismatch. Terms set by construction (inputs, pointers to objects, results of calls whose term
    /// has the format of the call's type) are not checked, and a missing term goes unnoticed (interp/README.md,
    /// "Terms"). Tests also pass checks that find mismatches on purpose, to see how a mismatch ends the run.
    TermCheck checkTerms = nullptr;
    /// Whether a run that enters an endless loop without effect ends (Termination::EndlessLoop) after one round of it,
    /// instead of running for ever as the program does natively. Validation sets it; replay does not.
    bool endsEndlessLoops = false;
};

enum class Termination : std::uint8_t {
    Returned,          // main returned; exitStatus is its value
    Exit,              // exit() or _Exit(); exitStatus is the argument
    Abort,             // abort(), or __assert_fail() (assert)
    AssumeFailed,      // __VERIFIER_assume with a false argument: the execution is infeasible
    Stopped,           // the observer returned Directive::Stop
    UndefinedBehavior, // an operation without a defined result (memory error, division by zero, ...)
    Unsupported,       // a construct the interpreter does not support (inline assembly, threads, ...)
    OutOfMemory,       // memoryLimit exceeded
    LimitReached,      // maxSteps or maxCallDepth exceeded
    EndlessLoop,       // RunOptions::endsEndlessLoops: the run entered a loop that never leaves and changes nothing
};

struct RunResult {
    Termination termination = Termination::Returned;
    int exitStatus = 0;
    std::string message;     // what went wrong, for UndefinedBehavior, Unsupported and limits
    SourceLocation location; // where it went wrong
    std::uint64_t steps = 0;
    std::uint32_t inputs = 0; // number of input requests
};

/// One execution of the program from `main`. Not reusable: create a new Interpreter per run.
class Interpreter {
public:
    Interpreter(std::shared_ptr<const Program> program, InputOracle& oracle, ExecutionObserver& observer,
                RunOptions options);
    ~Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    /// Runs the program. Calls recurse on the host stack, so call this on a thread with a large stack
    /// (support::runWithLargeStack) for deeply recursive programs.
    RunResult run();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace nitwit::interp
