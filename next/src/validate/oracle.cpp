#include "validate/oracle.hpp"

#include <algorithm>
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
    ++run_;
    fingerprints_.push_back(fingerprint_);
}

void WitnessOracle::hint(const ast::CallExpr* call, value::Value value) {
    hint_ = Hint{.call = call, .value = std::move(value)};
}

void WitnessOracle::fix(InputKey key, value::Value value) {
    History& history = fixed_[key];
    if (!history.values.empty()) {
        fingerprint_ -= hashOf(key, history.values.back());
    }
    fingerprint_ += hashOf(key, value);
    history.values.push_back(std::move(value));
    history.runs.push_back(run_ + 1);
}

std::span<const value::Value> WitnessOracle::fixedValues(InputKey key) const {
    const auto found = fixed_.find(key);
    return found != fixed_.end() ? std::span<const value::Value>(found->second.values)
                                 : std::span<const value::Value>();
}

std::optional<std::uint32_t> WitnessOracle::repeatedRun() const {
    for (std::uint32_t run = 1; run <= fingerprints_.size(); ++run) {
        if (fingerprints_[run - 1] != fingerprint_) {
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

std::uint64_t WitnessOracle::hashOf(const InputKey& key, const value::Value& value) {
    const SourceLocation location = key.site->location();
    const std::uint64_t site = (std::uint64_t{std::to_underlying(location.file)} << 32) | location.offset;
    return validate::hashOf(value) ^ (site * 0x9e3779b97f4a7c15ULL) ^ (std::uint64_t{key.occurrence} << 17);
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
        input.value = found->second.values.back();
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
