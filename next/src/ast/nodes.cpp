// The out-of-line members of types, declarations and expressions.

#include "ast/casting.hpp"
#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "ast/type.hpp"
#include "support/assert.hpp"

namespace nitwit::ast {

std::uint64_t ArrayType::size() const {
    NITWIT_ASSERT(sizeKind_ == SizeKind::Constant, "size of an array without a constant size");
    return size_;
}

void ArrayType::resolveConstantSize(std::uint64_t size) const {
    NITWIT_ASSERT(sizeKind_ == SizeKind::Unresolved, "array size resolved twice");
    sizeKind_ = SizeKind::Constant;
    size_ = size;
}

void ArrayType::resolveVariableSize() const {
    NITWIT_ASSERT(sizeKind_ == SizeKind::Unresolved, "array size resolved twice");
    sizeKind_ = SizeKind::Variable;
}

bool FieldDecl::isAnonymousRecord() const {
    return name().empty() && !isBitField() && isa<RecordType>(type().type());
}

void RecordDecl::complete(std::span<FieldDecl* const> fields) {
    NITWIT_ASSERT(!isComplete_, "record completed twice");
    fields_ = fields;
    isComplete_ = true;
}

FunctionDecl* CallExpr::directCallee() const {
    const Expr* callee = ignoreParens(callee_);
    if (const auto* decay = dyn_cast<ImplicitCastExpr>(callee);
        decay != nullptr && decay->castKind() == CastKind::FunctionToPointerDecay) {
        callee = ignoreParens(decay->operand());
    }
    if (const auto* reference = dyn_cast<DeclRefExpr>(callee)) {
        return dyn_cast<FunctionDecl>(reference->decl());
    }
    return nullptr;
}

Expr* ignoreParens(Expr* expr) {
    while (auto* paren = dyn_cast<ParenExpr>(expr)) {
        expr = paren->inner();
    }
    return expr;
}

const Expr* ignoreParens(const Expr* expr) {
    while (const auto* paren = dyn_cast<ParenExpr>(expr)) {
        expr = paren->inner();
    }
    return expr;
}

} // namespace nitwit::ast
