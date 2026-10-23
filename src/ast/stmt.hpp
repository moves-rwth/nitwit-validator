#pragma once

// Statements. A statement's range begins at its first token, which is the keyword for keyword statements; witness
// waypoints refer to these locations.

#include <cstdint>
#include <span>

#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "support/int128.hpp"
#include "support/source_manager.hpp"

namespace nitwit::ast {

enum class StmtKind : std::uint8_t {
    Compound,
    Expression,
    Declaration,
    If,
    While,
    DoWhile,
    For,
    Switch,
    Case,
    Default,
    Break,
    Continue,
    Return,
    Goto,
    Label,
    Null,
    Asm,
};

class Stmt {
public:
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    [[nodiscard]] StmtKind kind() const { return kind_; }
    [[nodiscard]] SourceRange range() const { return range_; }
    [[nodiscard]] SourceLocation location() const { return range_.begin; }

protected:
    Stmt(StmtKind kind, SourceRange range) : kind_(kind), range_(range) {}
    ~Stmt() = default;

private:
    StmtKind kind_;
    SourceRange range_;
};

/// `{ ... }`: a block, which opens a scope.
class CompoundStmt final : public Stmt {
public:
    CompoundStmt(SourceRange range, std::span<Stmt* const> body) : Stmt(StmtKind::Compound, range), body_(body) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Compound; }

    [[nodiscard]] std::span<Stmt* const> body() const { return body_; }

private:
    std::span<Stmt* const> body_;
};

class ExpressionStmt final : public Stmt {
public:
    ExpressionStmt(SourceRange range, Expr* expr) : Stmt(StmtKind::Expression, range), expr_(expr) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Expression; }

    [[nodiscard]] Expr* expr() const { return expr_; }
    void setExpr(Expr* expr) { expr_ = expr; }

private:
    Expr* expr_;
};

/// A declaration in a block, e.g. `int x = 1, *p;`, a typedef, a tag or a function prototype.
class DeclarationStmt final : public Stmt {
public:
    DeclarationStmt(SourceRange range, std::span<Decl* const> decls)
        : Stmt(StmtKind::Declaration, range), decls_(decls) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Declaration; }

    [[nodiscard]] std::span<Decl* const> decls() const { return decls_; }

private:
    std::span<Decl* const> decls_;
};

class IfStmt final : public Stmt {
public:
    IfStmt(SourceRange range, Expr* condition, Stmt* thenStmt, Stmt* elseStmt)
        : Stmt(StmtKind::If, range), condition_(condition), then_(thenStmt), else_(elseStmt) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::If; }

    [[nodiscard]] Expr* condition() const { return condition_; }
    void setCondition(Expr* condition) { condition_ = condition; }
    [[nodiscard]] Stmt* thenStmt() const { return then_; }
    /// Null without `else`.
    [[nodiscard]] Stmt* elseStmt() const { return else_; }

private:
    Expr* condition_;
    Stmt* then_;
    Stmt* else_;
};

class WhileStmt final : public Stmt {
public:
    WhileStmt(SourceRange range, Expr* condition, Stmt* body)
        : Stmt(StmtKind::While, range), condition_(condition), body_(body) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::While; }

    [[nodiscard]] Expr* condition() const { return condition_; }
    void setCondition(Expr* condition) { condition_ = condition; }
    [[nodiscard]] Stmt* body() const { return body_; }

private:
    Expr* condition_;
    Stmt* body_;
};

class DoWhileStmt final : public Stmt {
public:
    DoWhileStmt(SourceRange range, Stmt* body, Expr* condition, SourceLocation whileLocation)
        : Stmt(StmtKind::DoWhile, range), body_(body), condition_(condition), whileLocation_(whileLocation) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::DoWhile; }

    [[nodiscard]] Stmt* body() const { return body_; }
    [[nodiscard]] Expr* condition() const { return condition_; }
    void setCondition(Expr* condition) { condition_ = condition; }
    [[nodiscard]] SourceLocation whileLocation() const { return whileLocation_; }

private:
    Stmt* body_;
    Expr* condition_;
    SourceLocation whileLocation_;
};

/// A `for` loop. A declaration in `init` is scoped to the loop.
class ForStmt final : public Stmt {
public:
    ForStmt(SourceRange range, Stmt* init, Expr* condition, Expr* increment, Stmt* body)
        : Stmt(StmtKind::For, range), init_(init), condition_(condition), increment_(increment), body_(body) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::For; }

    /// An ExpressionStmt, a DeclarationStmt, or null.
    [[nodiscard]] Stmt* init() const { return init_; }
    /// Null if omitted (an infinite loop).
    [[nodiscard]] Expr* condition() const { return condition_; }
    void setCondition(Expr* condition) { condition_ = condition; }
    [[nodiscard]] Expr* increment() const { return increment_; }
    void setIncrement(Expr* increment) { increment_ = increment; }
    [[nodiscard]] Stmt* body() const { return body_; }

private:
    Stmt* init_;
    Expr* condition_;
    Expr* increment_;
    Stmt* body_;
};

class CaseStmt;
class DefaultStmt;

class SwitchStmt final : public Stmt {
public:
    SwitchStmt(SourceRange range, Expr* condition, Stmt* body)
        : Stmt(StmtKind::Switch, range), condition_(condition), body_(body) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Switch; }

    [[nodiscard]] Expr* condition() const { return condition_; }
    void setCondition(Expr* condition) { condition_ = condition; }
    [[nodiscard]] Stmt* body() const { return body_; }

    /// The case labels belonging to this switch (not to nested ones), in source order; set by the parser.
    [[nodiscard]] std::span<CaseStmt* const> cases() const { return cases_; }
    [[nodiscard]] DefaultStmt* defaultStmt() const { return default_; }
    void setLabels(std::span<CaseStmt* const> cases, DefaultStmt* defaultStmt) {
        cases_ = cases;
        default_ = defaultStmt;
    }

private:
    Expr* condition_;
    Stmt* body_;
    std::span<CaseStmt* const> cases_;
    DefaultStmt* default_ = nullptr;
};

/// `case value:` or GNU's `case low ... high:`.
class CaseStmt final : public Stmt {
public:
    CaseStmt(SourceRange range, Expr* value, Expr* rangeEnd, Stmt* subStmt)
        : Stmt(StmtKind::Case, range), value_(value), rangeEnd_(rangeEnd), sub_(subStmt) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Case; }

    [[nodiscard]] Expr* value() const { return value_; }
    /// Null unless this is a case range.
    [[nodiscard]] Expr* rangeEnd() const { return rangeEnd_; }
    [[nodiscard]] Stmt* subStmt() const { return sub_; }

    /// Sema: the values converted to the promoted type of the switch condition (C17 6.8.4.2p5).
    [[nodiscard]] Int128 low() const { return low_; }
    [[nodiscard]] Int128 high() const { return high_; }
    void setValues(Int128 low, Int128 high) {
        low_ = low;
        high_ = high;
    }

private:
    Expr* value_;
    Expr* rangeEnd_;
    Stmt* sub_;
    Int128 low_ = 0;
    Int128 high_ = 0;
};

class DefaultStmt final : public Stmt {
public:
    DefaultStmt(SourceRange range, Stmt* subStmt) : Stmt(StmtKind::Default, range), sub_(subStmt) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Default; }

    [[nodiscard]] Stmt* subStmt() const { return sub_; }

private:
    Stmt* sub_;
};

class BreakStmt final : public Stmt {
public:
    explicit BreakStmt(SourceRange range) : Stmt(StmtKind::Break, range) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Break; }
};

class ContinueStmt final : public Stmt {
public:
    explicit ContinueStmt(SourceRange range) : Stmt(StmtKind::Continue, range) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Continue; }
};

class ReturnStmt final : public Stmt {
public:
    ReturnStmt(SourceRange range, Expr* value) : Stmt(StmtKind::Return, range), value_(value) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Return; }

    /// Null for `return;`.
    [[nodiscard]] Expr* value() const { return value_; }
    void setValue(Expr* value) { value_ = value; }

private:
    Expr* value_;
};

class LabelStmt;

class GotoStmt final : public Stmt {
public:
    GotoStmt(SourceRange range, Symbol label) : Stmt(StmtKind::Goto, range), label_(label) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Goto; }

    [[nodiscard]] Symbol label() const { return label_; }
    /// Resolved by the parser at the end of the function.
    [[nodiscard]] LabelStmt* target() const { return target_; }
    void setTarget(LabelStmt* target) { target_ = target; }

private:
    Symbol label_;
    LabelStmt* target_ = nullptr;
};

class LabelStmt final : public Stmt {
public:
    LabelStmt(SourceRange range, Symbol name, Stmt* subStmt)
        : Stmt(StmtKind::Label, range), name_(name), sub_(subStmt) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Label; }

    [[nodiscard]] Symbol name() const { return name_; }
    [[nodiscard]] Stmt* subStmt() const { return sub_; }

private:
    Symbol name_;
    Stmt* sub_;
};

class NullStmt final : public Stmt {
public:
    explicit NullStmt(SourceRange range) : Stmt(StmtKind::Null, range) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Null; }
};

/// GNU inline assembly. Only the template is kept: the interpreter treats an empty template (a compiler barrier such
/// as `__asm__ __volatile__("" ::: "memory")`) as a no-op and rejects anything else.
class AsmStmt final : public Stmt {
public:
    AsmStmt(SourceRange range, StringLiteralExpr* asmTemplate) : Stmt(StmtKind::Asm, range), template_(asmTemplate) {}
    static bool classof(const Stmt* stmt) { return stmt->kind() == StmtKind::Asm; }

    [[nodiscard]] StringLiteralExpr* asmTemplate() const { return template_; }

private:
    StringLiteralExpr* template_;
};

} // namespace nitwit::ast
