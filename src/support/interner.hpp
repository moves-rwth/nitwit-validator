#pragma once

// Interned strings for identifiers: comparing two symbols from the same interner compares their texts.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace nitwit {

class Symbol {
public:
    /// The empty symbol, whose text is "".
    Symbol() = default;

    [[nodiscard]] std::string_view text() const { return entry_ != nullptr ? std::string_view(*entry_) : ""; }
    [[nodiscard]] bool empty() const { return entry_ == nullptr; }

    friend bool operator==(Symbol, Symbol) = default;

private:
    friend class StringInterner;
    friend struct std::hash<Symbol>;

    explicit Symbol(const std::string* entry) : entry_(entry) {}

    const std::string* entry_ = nullptr;
};

class StringInterner {
public:
    /// The symbol for `text`; interning the empty string gives the empty symbol.
    Symbol intern(std::string_view text);

private:
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
    };

    // Nodes of an unordered_set never move, so symbols can point to the strings.
    std::unordered_set<std::string, Hash, std::equal_to<>> strings_;
};

} // namespace nitwit

template <>
struct std::hash<nitwit::Symbol> {
    std::size_t operator()(nitwit::Symbol symbol) const { return std::hash<const std::string*>{}(symbol.entry_); }
};
