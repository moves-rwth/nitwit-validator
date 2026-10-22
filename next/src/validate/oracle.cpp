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
    fixed_[key].push_back(std::move(value));
}

std::span<const value::Value> WitnessOracle::fixedValues(InputKey key) const {
    const auto found = fixed_.find(key);
    return found != fixed_.end() ? std::span<const value::Value>(found->second) : std::span<const value::Value>();
}

value::Value WitnessOracle::provide(const interp::InputRequest& request) {
    NITWIT_ASSERT(static_cast<std::size_t>(request.id) == inputs_.size(), "inputs are numbered in request order");
    ProvidedInput& input =
        inputs_.emplace_back(ProvidedInput{.key = {.site = request.site, .occurrence = request.occurrence},
                                           .type = request.type,
                                           .source = InputSource::Default,
                                           .function = request.function,
                                           .value = {}});
    if (const auto found = fixed_.find(input.key); found != fixed_.end()) {
        input.source = InputSource::Fixed;
        input.value = found->second.back();
    } else if (hint_ && hint_->call == request.site) {
        input.source = InputSource::Hinted;
        input.value = std::move(hint_->value);
        hint_.reset();
    } else if (isArithmetic(context_, request.type)) {
        input.value = valueOf(context_, defaultValue_, request.type);
    } else {
        input.value = value::Value::integer(0, sema::integerFormat(context_, request.type)); // a pointer: null
    }
    return input.value;
}

const ProvidedInput& WitnessOracle::input(value::InputId id) const {
    const auto index = static_cast<std::size_t>(id);
    NITWIT_ASSERT(index < inputs_.size(), "an input the run did not request");
    return inputs_[index];
}

} // namespace nitwit::validate
