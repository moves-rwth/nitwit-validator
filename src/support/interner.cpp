#include "support/interner.hpp"

namespace nitwit {

Symbol StringInterner::intern(std::string_view text) {
    if (text.empty()) {
        return Symbol{};
    }
    auto found = strings_.find(text);
    if (found == strings_.end()) {
        found = strings_.emplace(text).first;
    }
    return Symbol{&*found};
}

} // namespace nitwit
