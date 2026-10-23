#include "validate/oracle.hpp"

#include <algorithm>
#include <utility>

#include "sema/types.hpp"
#include "support/assert.hpp"
#include "validate/values.hpp"

namespace nitwit::validate {

WitnessOracle::WitnessOracle(ast::AstContext& context, Int128 defaultValue, std::size_t recordedInputs)
    : context_(context), defaultValue_(defaultValue), recordedInputs_(recordedInputs) {}

void WitnessOracle::beginRun() {
    inputs_.clear();
    hint_.reset();
    gaveDefault_ = false;
    gaveFixedValue_ = false;
    ++run_;
    runStarts_.push_back(next_);
}

void WitnessOracle::hint(const ast::CallExpr* call, value::Value value) {
    hint_ = Hint{.call = call, .value = std::move(value)};
}

void WitnessOracle::fix(InputKey key, value::Value value) {
    History& history = fixed_[key];
    if (!history.values.empty()) {
        next_.fingerprint -= hashOf(history.values.back());
    }
    next_.fingerprint += hashOf(value);
    if (std::ranges::none_of(history.distinct, [&](const value::Value& old) { return isSame(old, value); })) {
        history.distinct.push_back(value);
        ++next_.distinctValues;
    }
    history.values.push_back(std::move(value));
    history.runs.push_back(run_ + 1);
}

std::span<const value::Value> WitnessOracle::fixedValues(InputKey key) const {
    const auto found = fixed_.find(key);
    return found != fixed_.end() ? std::span<const value::Value>(found->second.distinct)
                                 : std::span<const value::Value>();
}

// The number of distinct values only grows: if it is the same as at an earlier run, the keys were fixed to the same
// values before both runs.
std::optional<std::uint32_t> WitnessOracle::repeatedRun() const {
    for (std::uint32_t run = 1; run <= runStarts_.size(); ++run) {
        const RunStart& start = runStarts_[run - 1];
        if (start.fingerprint != next_.fingerprint || start.distinctValues != next_.distinctValues) {
            continue;
        }
        // Keys are never unfixed, so the run had the same values if each key had its value now then.
        const auto isUnchanged = [&](const auto& entry) {
            const History& history = entry.second;
            const auto later = std::ranges::upper_bound(history.runs, run);
            const auto index = static_cast<std::size_t>(later - history.runs.begin());
            return index > 0 && isSame(history.values[index - 1], history.values.back());
        };
        if (std::ranges::all_of(fixed_, isUnchanged)) {
            return run;
        }
    }
    return std::nullopt;
}

value::Value WitnessOracle::provide(const interp::InputRequest& request) {
    const auto index = static_cast<std::size_t>(request.id);
    NITWIT_ASSERT(index == inputs_.size() || (index >= recordedInputs_ && inputs_.size() == recordedInputs_),
                  "inputs are numbered in request order");
    ProvidedInput input{.key = {.site = request.site, .occurrence = request.occurrence},
                        .type = request.type,
                        .source = InputSource::Default,
                        .function = request.function,
                        .value = {}};
    if (const auto found = fixed_.find(input.key); found != fixed_.end()) {
        input.source = InputSource::Fixed;
        input.value = found->second.values.back();
    } else if (hint_ && hint_->call == request.site) {
        input.source = InputSource::Hinted;
        input.value = std::move(hint_->value);
        hint_.reset();
    } else if (isArithmetic(context_, request.type)) {
        input.value = valueOf(context_, defaultValue_, request.type);
        gaveDefault_ = true;
    } else {
        input.value = value::Value::integer(0, sema::integerFormat(context_, request.type)); // a pointer: null
    }
    if (index >= recordedInputs_) {
        return input.value;
    }
    gaveFixedValue_ = gaveFixedValue_ || input.source == InputSource::Fixed;
    return inputs_.emplace_back(std::move(input)).value;
}

const ProvidedInput& WitnessOracle::input(value::InputId id) const {
    const auto index = static_cast<std::size_t>(id);
    NITWIT_ASSERT(index < inputs_.size(), "an input the run did not request, or one beyond the recorded ones");
    return inputs_[index];
}

} // namespace nitwit::validate
