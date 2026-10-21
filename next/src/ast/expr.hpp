#pragma once

// Expressions. The parser builds them with names bound to declarations. Semantic analysis then fills in the
// remaining fields:
// - the type and value category of every expression;
// - the field of every MemberExpr;
// - ImplicitCastExprs for every implicit conversion;
// - the semantic form of initializer lists.
// After sema, the interpreter never has to apply a C typing rule itself.

#include <cstdint>
#include <span>

#include "ast/decl.hpp"
#include "ast/type.hpp"
#include "lex/literals.hpp"
#include "support/interner.hpp"
#include "support/source_manager.hpp"

namespace nitwit::ast {

class CompoundStmt;
class ExpressionStmt;

enum class ExprKind : std::uint8_t {
    IntegerLiteral,
    FloatingLiteral,
    CharacterLiteral,
    StringLiteral,
    DeclRef,
    Paren,
    Unary,
    Binary,
    Conditional,
    Call,
    Member,
    ArraySubscript,
    Cast,
    ImplicitCast,
    CompoundLiteral,
    InitList,
    DesignatedInit,
    ImplicitZero,
    SizeOfAlignOf,
    OffsetOf,
    VaArg,
    StatementExpr,
    GenericSelection,
    ChooseExpr,
    TypesCompatible,
    Result,
};

enum class ValueCategory : std::uint8_t { RValue, LValue };

class Expr {
public:
    Expr(const Expr&) = delete;
    Expr& operator=(const Expr&) = delete;

    [[nodiscard]] ExprKind kind() const { return kind_; }
    [[nodiscard]] SourceRange range() const { return range_; }
    [[nodiscard]] SourceLocation location() const { return range_.begin; }

    /// Set by sema; null before.
    [[nodiscard]] QualType type() const { return type_; }
    [[nodiscard]] ValueCategory category() const { return category_; }
    [[nodiscard]] bool isLValue() const { return category_ == ValueCategory::LValue; }
    void setType(QualType type, ValueCategory category) {
        type_ = type;
        category_ = category;
    }

protected:
    Expr(ExprKind kind, SourceRange range) : kind_(kind), range_(range) {}
    ~Expr() = default;

private:
    ExprKind kind_;
    ValueCategory category_ = ValueCategory::RValue;
    SourceRange range_;
    QualType type_;
};

class IntegerLiteralExpr final : public Expr {
public:
    IntegerLiteralExpr(SourceRange range, lex::IntegerLiteral value)
        : Expr(ExprKind::IntegerLiteral, range), value_(value) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::IntegerLiteral; }

    [[nodiscard]] const lex::IntegerLiteral& value() const { return value_; }

private:
    lex::IntegerLiteral value_;
};

class FloatingLiteralExpr final : public Expr {
public:
    FloatingLiteralExpr(SourceRange range, lex::FloatingLiteral value)
        : Expr(ExprKind::FloatingLiteral, range), value_(value) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::FloatingLiteral; }

    [[nodiscard]] const lex::FloatingLiteral& value() const { return value_; }

private:
    lex::FloatingLiteral value_;
};

class CharacterLiteralExpr final : public Expr {
public:
    CharacterLiteralExpr(SourceRange range, lex::CharacterLiteral value)
        : Expr(ExprKind::CharacterLiteral, range), value_(value) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::CharacterLiteral; }

    [[nodiscard]] const lex::CharacterLiteral& value() const { return value_; }

private:
    lex::CharacterLiteral value_;
};

/// One or more adjacent string literals, concatenated; also `__func__` and its GNU spellings.
class StringLiteralExpr final : public Expr {
public:
    StringLiteralExpr(SourceRange range, lex::CharEncoding encoding, std::span<const std::uint32_t> codeUnits)
        : Expr(ExprKind::StringLiteral, range), encoding_(encoding), codeUnits_(codeUnits) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::StringLiteral; }

    [[nodiscard]] lex::CharEncoding encoding() const { return encoding_; }
    /// Without the terminating zero.
    [[nodiscard]] std::span<const std::uint32_t> codeUnits() const { return codeUnits_; }

private:
    lex::CharEncoding encoding_;
    std::span<const std::uint32_t> codeUnits_;
};

/// A use of a variable, function or enumeration constant. The declaration is null only for identifiers in attribute
/// arguments, e.g. `mode(__QI__)`.
class DeclRefExpr final : public Expr {
public:
    DeclRefExpr(SourceRange range, Symbol name, ValueDecl* decl)
        : Expr(ExprKind::DeclRef, range), name_(name), decl_(decl) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::DeclRef; }

    [[nodiscard]] Symbol name() const { return name_; }
    [[nodiscard]] ValueDecl* decl() const { return decl_; }

private:
    Symbol name_;
    ValueDecl* decl_;
};

class ParenExpr final : public Expr {
public:
    ParenExpr(SourceRange range, Expr* inner) : Expr(ExprKind::Paren, range), inner_(inner) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::Paren; }

    [[nodiscard]] Expr* inner() const { return inner_; }

private:
    Expr* inner_;
};

enum class UnaryOperator : std::uint8_t {
    PostIncrement,
    PostDecrement,
    PreIncrement,
    PreDecrement,
    AddressOf,
    Dereference,
    Plus,
    Minus,
    BitwiseNot,
    LogicalNot,
    Real,      // __real__
    Imag,      // __imag__
    Extension, // __extension__ in an expression; no effect
};

class UnaryExpr final : public Expr {
public:
    UnaryExpr(SourceRange range, UnaryOperator op, Expr* operand)
        : Expr(ExprKind::Unary, range), op_(op), operand_(operand) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::Unary; }

    [[nodiscard]] UnaryOperator op() const { return op_; }
    [[nodiscard]] Expr* operand() const { return operand_; }
    void setOperand(Expr* operand) { operand_ = operand; }

private:
    UnaryOperator op_;
    Expr* operand_;
};

enum class BinaryOperator : std::uint8_t {
    Mul,
    Div,
    Rem,
    Add,
    Sub,
    Shl,
    Shr,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Equal,
    NotEqual,
    BitwiseAnd,
    BitwiseXor,
    BitwiseOr,
    LogicalAnd,
    LogicalOr,
    Assign,
    MulAssign,
    DivAssign,
    RemAssign,
    AddAssign,
    SubAssign,
    ShlAssign,
    ShrAssign,
    AndAssign,
    XorAssign,
    OrAssign,
    Comma,
};

class BinaryExpr final : public Expr {
public:
    BinaryExpr(SourceRange range, BinaryOperator op, Expr* lhs, Expr* rhs, SourceLocation operatorLocation)
        : Expr(ExprKind::Binary, range), op_(op), lhs_(lhs), rhs_(rhs), operatorLocation_(operatorLocation) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::Binary; }

    [[nodiscard]] BinaryOperator op() const { return op_; }
    [[nodiscard]] Expr* lhs() const { return lhs_; }
    [[nodiscard]] Expr* rhs() const { return rhs_; }
    void setOperands(Expr* lhs, Expr* rhs) {
        lhs_ = lhs;
        rhs_ = rhs;
    }
    [[nodiscard]] SourceLocation operatorLocation() const { return operatorLocation_; }

    /// Sema, for compound assignments: the type in which the operation is performed (C17 6.5.16.2p3). The left
    /// operand's value is converted to it, the operation is performed, and the result is converted back to the type
    /// of the left operand. For pointer arithmetic (`p += n`) it is the pointer type.
    [[nodiscard]] QualType computationType() const { return computationType_; }
    void setComputationType(QualType type) { computationType_ = type; }

private:
    BinaryOperator op_;
    Expr* lhs_;
    Expr* rhs_;
    SourceLocation operatorLocation_;
    QualType computationType_;
};

/// `c ? a : b`, and GNU's `c ?: b` (trueExpr() is null; the condition's value is the result if it is non-zero).
class ConditionalExpr final : public Expr {
public:
    ConditionalExpr(SourceRange range, Expr* condition, Expr* trueExpr, Expr* falseExpr,
                    SourceLocation questionLocation)
        : Expr(ExprKind::Conditional, range), condition_(condition), trueExpr_(trueExpr), falseExpr_(falseExpr),
          questionLocation_(questionLocation) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::Conditional; }

    [[nodiscard]] Expr* condition() const { return condition_; }
    [[nodiscard]] Expr* trueExpr() const { return trueExpr_; }
    [[nodiscard]] Expr* falseExpr() const { return falseExpr_; }
    void setOperands(Expr* condition, Expr* trueExpr, Expr* falseExpr) {
        condition_ = condition;
        trueExpr_ = trueExpr;
        falseExpr_ = falseExpr;
    }
    /// The `?`, where witness format 2.x locates branching waypoints of a conditional expression.
    [[nodiscard]] SourceLocation questionLocation() const { return questionLocation_; }

private:
    Expr* condition_;
    Expr* trueExpr_;
    Expr* falseExpr_;
    SourceLocation questionLocation_;
};

class CallExpr final : public Expr {
public:
    CallExpr(SourceRange range, Expr* callee, std::span<Expr*> arguments, SourceLocation rightParenLocation)
        : Expr(ExprKind::Call, range), callee_(callee), arguments_(arguments), rightParenLocation_(rightParenLocation) {
    }
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::Call; }

    [[nodiscard]] Expr* callee() const { return callee_; }
    void setCallee(Expr* callee) { callee_ = callee; }
    /// Sema replaces arguments by their converted forms in place.
    [[nodiscard]] std::span<Expr*> arguments() const { return arguments_; }
    /// The closing `)`, where witness format 2.x locates function_enter and function_return waypoints.
    [[nodiscard]] SourceLocation rightParenLocation() const { return rightParenLocation_; }

    /// The called function if the callee is (after sema: a decay of) a function designator; null for calls through
    /// pointers.
    [[nodiscard]] FunctionDecl* directCallee() const;

private:
    Expr* callee_;
    std::span<Expr*> arguments_;
    SourceLocation rightParenLocation_;
};

/// `base.member` or `base->member`. Sema resolves the field; for a member of an anonymous struct or union, sema
/// rewrites the base into an implicit MemberExpr for the anonymous member, so that `field()` is always a direct field
/// of the base's record.
class MemberExpr final : public Expr {
public:
    MemberExpr(SourceRange range, Expr* base, Symbol member, bool isArrow, SourceLocation memberLocation)
        : Expr(ExprKind::Member, range), base_(base), member_(member), isArrow_(isArrow),
          memberLocation_(memberLocation) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::Member; }

    [[nodiscard]] Expr* base() const { return base_; }
    void setBase(Expr* base) { base_ = base; }
    [[nodiscard]] Symbol member() const { return member_; }
    [[nodiscard]] bool isArrow() const { return isArrow_; }
    /// Sema: a rewritten `p->x` becomes `.x` of the implicit member expression for the anonymous member.
    void setArrow(bool isArrow) { isArrow_ = isArrow; }
    [[nodiscard]] SourceLocation memberLocation() const { return memberLocation_; }
    [[nodiscard]] FieldDecl* field() const { return field_; }
    void setField(FieldDecl* field) { field_ = field; }

private:
    Expr* base_;
    Symbol member_;
    bool isArrow_;
    SourceLocation memberLocation_;
    FieldDecl* field_ = nullptr;
};

/// `a[i]`. After sema, base() has pointer type and index() integer type; sema swaps the operands of `i[a]`.
class ArraySubscriptExpr final : public Expr {
public:
    ArraySubscriptExpr(SourceRange range, Expr* base, Expr* index)
        : Expr(ExprKind::ArraySubscript, range), base_(base), index_(index) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::ArraySubscript; }

    [[nodiscard]] Expr* base() const { return base_; }
    [[nodiscard]] Expr* index() const { return index_; }
    void setOperands(Expr* base, Expr* index) {
        base_ = base;
        index_ = index;
    }

private:
    Expr* base_;
    Expr* index_;
};

enum class CastKind : std::uint8_t {
    LValueToRValue,
    ArrayToPointerDecay,
    FunctionToPointerDecay,
    NoOp,         // only qualifiers change, or a cast to the same type
    IntegralCast, // between integer types other than _Bool, including enums
    IntegralToBoolean,
    IntegralToFloating,
    IntegralToPointer,
    PointerToIntegral,
    PointerToBoolean,
    NullToPointer, // a null pointer constant to a pointer type
    BitCast,       // pointer to pointer
    FloatingCast,
    FloatingToIntegral,
    FloatingToBoolean,
    ToVoid,
    ToUnion, // GNU cast to a union, also a transparent-union argument: the operand initializes the first member whose
             // type is compatible with the operand's (sema converts an argument to that member's type first)
};

/// A cast written in the source, `(T)e`. Sema sets the cast kind.
class CastExpr final : public Expr {
public:
    CastExpr(SourceRange range, QualType writtenType, Expr* operand)
        : Expr(ExprKind::Cast, range), writtenType_(writtenType), operand_(operand) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::Cast; }

    [[nodiscard]] QualType writtenType() const { return writtenType_; }
    [[nodiscard]] Expr* operand() const { return operand_; }
    void setOperand(Expr* operand) { operand_ = operand; }
    [[nodiscard]] CastKind castKind() const { return castKind_; }
    void setCastKind(CastKind kind) { castKind_ = kind; }

private:
    QualType writtenType_;
    Expr* operand_;
    CastKind castKind_ = CastKind::NoOp;
};

/// An implicit conversion inserted by sema; its range is the operand's.
class ImplicitCastExpr final : public Expr {
public:
    ImplicitCastExpr(CastKind castKind, Expr* operand)
        : Expr(ExprKind::ImplicitCast, operand->range()), castKind_(castKind), operand_(operand) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::ImplicitCast; }

    [[nodiscard]] CastKind castKind() const { return castKind_; }
    [[nodiscard]] Expr* operand() const { return operand_; }

private:
    CastKind castKind_;
    Expr* operand_;
};

class InitListExpr;

/// `(T){ ... }`: an lvalue with static storage at file scope, automatic storage otherwise.
class CompoundLiteralExpr final : public Expr {
public:
    CompoundLiteralExpr(SourceRange range, QualType writtenType, InitListExpr* initializer, bool isFileScope)
        : Expr(ExprKind::CompoundLiteral, range), writtenType_(writtenType), initializer_(initializer),
          isFileScope_(isFileScope) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::CompoundLiteral; }

    [[nodiscard]] QualType writtenType() const { return writtenType_; }
    [[nodiscard]] InitListExpr* initializer() const { return initializer_; }
    [[nodiscard]] bool isFileScope() const { return isFileScope_; }

private:
    QualType writtenType_;
    InitListExpr* initializer_;
    bool isFileScope_;
};

/// A braced initializer.
///
/// Syntactic form (parser): the elements as written; elements may be DesignatedInitExprs.
///
/// Semantic form (after sema), whose type is the initialized aggregate:
/// - struct: one element per field, in field order; ImplicitZeroExpr where the field is not initialized, also for
///   unnamed bit-fields;
/// - union: zero or one element, initializing `unionField()`;
/// - array: elements in index order up to the last initialized index (trailing elements are zero, see
///   ArrayType::size()); ImplicitZeroExpr in gaps; a GNU range designator repeats the same Expr*;
/// - nested aggregates have their own semantic InitListExpr (brace elision is resolved); a char array initialized from
///   a string literal has the StringLiteralExpr as its element (or as the whole initializer).
/// A braced scalar initializer (`int x = {1};`) is replaced by its element.
class InitListExpr final : public Expr {
public:
    InitListExpr(SourceRange range, std::span<Expr*> elements) : Expr(ExprKind::InitList, range), elements_(elements) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::InitList; }

    [[nodiscard]] std::span<Expr*> elements() const { return elements_; }
    void setElements(std::span<Expr*> elements) { elements_ = elements; }
    [[nodiscard]] FieldDecl* unionField() const { return unionField_; }
    void setUnionField(FieldDecl* field) { unionField_ = field; }
    [[nodiscard]] bool isSemanticForm() const { return isSemanticForm_; }
    void setSemanticForm() { isSemanticForm_ = true; }

private:
    std::span<Expr*> elements_;
    FieldDecl* unionField_ = nullptr;
    bool isSemanticForm_ = false;
};

struct Designator {
    enum class Kind : std::uint8_t { Field, Index, Range };

    Kind kind = Kind::Field;
    SourceLocation location;
    Symbol fieldName;      // Field
    Expr* first = nullptr; // Index, Range
    Expr* last = nullptr;  // Range (GNU `[first ... last]`)
};

/// `.a[2] = value` and GNU's `a: value` inside a syntactic InitListExpr; sema removes these.
class DesignatedInitExpr final : public Expr {
public:
    DesignatedInitExpr(SourceRange range, std::span<const Designator> designators, Expr* initializer)
        : Expr(ExprKind::DesignatedInit, range), designators_(designators), initializer_(initializer) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::DesignatedInit; }

    [[nodiscard]] std::span<const Designator> designators() const { return designators_; }
    [[nodiscard]] Expr* initializer() const { return initializer_; }

private:
    std::span<const Designator> designators_;
    Expr* initializer_;
};

/// Zero-initialization of an element or member without an initializer, in a semantic InitListExpr.
class ImplicitZeroExpr final : public Expr {
public:
    explicit ImplicitZeroExpr(SourceRange range) : Expr(ExprKind::ImplicitZero, range) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::ImplicitZero; }
};

/// sizeof, _Alignof and GNU __alignof__ of an expression or a type.
class SizeOfAlignOfExpr final : public Expr {
public:
    enum class Operator : std::uint8_t { SizeOf, AlignOf, PreferredAlignOf }; // PreferredAlignOf: __alignof__

    SizeOfAlignOfExpr(SourceRange range, Operator op, Expr* operand)
        : Expr(ExprKind::SizeOfAlignOf, range), op_(op), operand_(operand) {}
    SizeOfAlignOfExpr(SourceRange range, Operator op, QualType typeOperand)
        : Expr(ExprKind::SizeOfAlignOf, range), op_(op), typeOperand_(typeOperand) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::SizeOfAlignOf; }

    [[nodiscard]] Operator op() const { return op_; }
    /// Null if the operand is a type.
    [[nodiscard]] Expr* operand() const { return operand_; }
    [[nodiscard]] QualType typeOperand() const { return typeOperand_; }

private:
    Operator op_;
    Expr* operand_ = nullptr;
    QualType typeOperand_;
};

/// `__builtin_offsetof(type, a.b[i].c)`.
class OffsetOfExpr final : public Expr {
public:
    struct Component {
        bool isField = true;
        Symbol fieldName;           // isField
        Expr* index = nullptr;      // !isField
        FieldDecl* field = nullptr; // sema
    };

    OffsetOfExpr(SourceRange range, QualType recordType, std::span<Component> components)
        : Expr(ExprKind::OffsetOf, range), recordType_(recordType), components_(components) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::OffsetOf; }

    [[nodiscard]] QualType recordType() const { return recordType_; }
    [[nodiscard]] std::span<Component> components() const { return components_; }

private:
    QualType recordType_;
    std::span<Component> components_;
};

/// `__builtin_va_arg(list, type)`.
class VaArgExpr final : public Expr {
public:
    VaArgExpr(SourceRange range, Expr* list, QualType argumentType)
        : Expr(ExprKind::VaArg, range), list_(list), argumentType_(argumentType) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::VaArg; }

    [[nodiscard]] Expr* list() const { return list_; }
    void setList(Expr* list) { list_ = list; }
    [[nodiscard]] QualType argumentType() const { return argumentType_; }

private:
    Expr* list_;
    QualType argumentType_;
};

/// GNU `({ ... })`: the value is that of the last expression statement in the block, or void.
class StatementExpr final : public Expr {
public:
    StatementExpr(SourceRange range, CompoundStmt* body) : Expr(ExprKind::StatementExpr, range), body_(body) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::StatementExpr; }

    [[nodiscard]] CompoundStmt* body() const { return body_; }
    /// The statement whose expression gives the value: the last statement of the body if it is an expression
    /// statement, also under labels (`({ ...; done: x; })`, as GCC); null if there is none.
    [[nodiscard]] ExpressionStmt* valueStatement() const;

private:
    CompoundStmt* body_;
};

/// C11 `_Generic`; sema selects the association.
class GenericSelectionExpr final : public Expr {
public:
    struct Association {
        QualType type; // null for `default`
        Expr* result = nullptr;
    };

    GenericSelectionExpr(SourceRange range, Expr* controlling, std::span<const Association> associations)
        : Expr(ExprKind::GenericSelection, range), controlling_(controlling), associations_(associations) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::GenericSelection; }

    [[nodiscard]] Expr* controlling() const { return controlling_; }
    [[nodiscard]] std::span<const Association> associations() const { return associations_; }
    /// Sema: the selected result expression.
    [[nodiscard]] Expr* selected() const { return selected_; }
    void setSelected(Expr* selected) { selected_ = selected; }

private:
    Expr* controlling_;
    std::span<const Association> associations_;
    Expr* selected_ = nullptr;
};

/// `__builtin_choose_expr(constant, a, b)`; sema selects a or b.
class ChooseExpr final : public Expr {
public:
    ChooseExpr(SourceRange range, Expr* condition, Expr* first, Expr* second)
        : Expr(ExprKind::ChooseExpr, range), condition_(condition), first_(first), second_(second) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::ChooseExpr; }

    [[nodiscard]] Expr* condition() const { return condition_; }
    [[nodiscard]] Expr* first() const { return first_; }
    [[nodiscard]] Expr* second() const { return second_; }
    [[nodiscard]] Expr* selected() const { return selected_; }
    void setSelected(Expr* selected) { selected_ = selected; }

private:
    Expr* condition_;
    Expr* first_;
    Expr* second_;
    Expr* selected_ = nullptr;
};

/// `__builtin_types_compatible_p(a, b)`; an int constant computed by sema.
class TypesCompatibleExpr final : public Expr {
public:
    TypesCompatibleExpr(SourceRange range, QualType first, QualType second)
        : Expr(ExprKind::TypesCompatible, range), first_(first), second_(second) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::TypesCompatible; }

    [[nodiscard]] QualType first() const { return first_; }
    [[nodiscard]] QualType second() const { return second_; }

private:
    QualType first_;
    QualType second_;
};

/// `\result` in a witness constraint: the return value of the call being observed.
class ResultExpr final : public Expr {
public:
    explicit ResultExpr(SourceRange range) : Expr(ExprKind::Result, range) {}
    static bool classof(const Expr* expr) { return expr->kind() == ExprKind::Result; }
};

/// Strips ParenExprs.
[[nodiscard]] Expr* ignoreParens(Expr* expr);
[[nodiscard]] const Expr* ignoreParens(const Expr* expr);

} // namespace nitwit::ast
