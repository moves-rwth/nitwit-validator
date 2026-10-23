#pragma once

// Kind-based downcasts for the AST class hierarchies: each class has `static bool classof(const Base*)`.

#include <type_traits>

#include "support/assert.hpp"

namespace nitwit::ast {

template <typename To, typename From>
[[nodiscard]] bool isa(const From* node) {
    NITWIT_ASSERT(node != nullptr, "isa on a null node");
    return To::classof(node);
}

template <typename To, typename From>
[[nodiscard]] auto cast(From* node) -> std::conditional_t<std::is_const_v<From>, const To*, To*> {
    NITWIT_ASSERT(isa<To>(node), "cast to the wrong node kind");
    return static_cast<std::conditional_t<std::is_const_v<From>, const To*, To*>>(node);
}

/// Null if `node` is null or not a `To`.
template <typename To, typename From>
[[nodiscard]] auto dyn_cast(From* node) -> std::conditional_t<std::is_const_v<From>, const To*, To*> {
    if (node == nullptr || !To::classof(node)) {
        return nullptr;
    }
    return static_cast<std::conditional_t<std::is_const_v<From>, const To*, To*>>(node);
}

} // namespace nitwit::ast
