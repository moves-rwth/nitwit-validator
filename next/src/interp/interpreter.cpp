#include "interp/interpreter.hpp"

#include "interp/execution.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

struct Interpreter::State {
    State(std::shared_ptr<const Program> program, InputOracle& oracle, ExecutionObserver& observer, RunOptions options)
        : execution(std::move(program), oracle, observer, options) {}

    Execution execution;
    bool hasRun = false;
};

Interpreter::Interpreter(std::shared_ptr<const Program> program, InputOracle& oracle, ExecutionObserver& observer,
                         RunOptions options)
    : state_(std::make_unique<State>(std::move(program), oracle, observer, options)) {}

Interpreter::~Interpreter() = default;

RunResult Interpreter::run() {
    NITWIT_ASSERT(!state_->hasRun, "an Interpreter runs once");
    state_->hasRun = true;
    return state_->execution.run();
}

} // namespace nitwit::interp
