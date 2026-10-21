#include "validate/oracle.hpp"

#include <utility>

#include "sema/types.hpp"
#include "support/assert.hpp"
#include "validate/values.hpp"

namespace nitwit::validate {

WitnessOracle::WitnessOracle(ast::AstContext& context, Int128 defaultValue)
    : context_(context), defaultValue_(defaultValue) {}

void WitnessOracle::beginRun() {
    inputs_.clear();
    hint_.reset();
}

void WitnessOracle::hint(const ast::CallExpr* call, value::Value value) {
    hint_ = Hint{.call = call, .value = std::move(value)};
}

void WitnessOracle::fix(InputKey key, value::Value value) {
    fixed_.insert_or_assign(key, std::move(value));
}

value::Value WitnessOracle::provide(const interp::InputRequest& request) {
    NITWIT_ASSERT(static_cast<std::size_t>(request.id) == inputs_.size(), "inputs are numbered in request order");
    ProvidedInput& input =
        inputs_.emplace_back(ProvidedInput{.key = {.site = request.site, .occurrence = request.occurrence},
                                           .type = request.type,
                                           .source = InputSource::Default,
                                           .function = request.function});
    if (const auto found = fixed_.find(input.key); found != fixed_.end()) {
        input.source = InputSource::Fixed;
        return found->second;
    }
    if (hint_ && hint_->call == request.site) {
        input.source = InputSource::Hinted;
        value::Value value = std::move(hint_->value);
        hint_.reset();
        return value;
    }
    if (isArithmetic(context_, request.type)) {
        return valueOf(context_, defaultValue_, request.type);
    }
    return value::Value::integer(0, sema::integerFormat(context_, request.type)); // a pointer: null
}

const ProvidedInput& WitnessOracle::input(value::InputId id) const {
    const auto index = static_cast<std::size_t>(id);
    NITWIT_ASSERT(index < inputs_.size(), "an input the run did not request");
    return inputs_[index];
}

} // namespace nitwit::validate
