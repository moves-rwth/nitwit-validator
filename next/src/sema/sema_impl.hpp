#pragma once

// The analyzer's state, shared by its source files; private to the module. sema.cpp has the translation unit and the
// diagnostics, declarations.cpp the declarations and their types, records.cpp structs, unions and enumerations,
// layout.cpp record layouts, expressions.cpp and operators.cpp the expressions, conversions.cpp the implicit
// conversions, initializers.cpp the initializers, statements.cpp the function bodies and builtins.cpp the builtin
// functions.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "ast/stmt.hpp"
#include "sema/type_rules.hpp"
#include "support/diagnostics.hpp"
#include "support/int128.hpp"

namespace nitwit::sema::detail {

/// Thrown after the first error has been reported; analyze() catches it.
struct SemaError {};

/// Where a value is converted as if by assignment (C17 6.5.16.1); it decides the wording of diagnostics.
struct AssignmentContext {
    enum class Kind : std::uint8_t { Assignment, Initialization, Argument, Return };

    Kind kind = Kind::Assignment;
    SourceLocation location;
    std::size_t argumentIndex = 0;               // Argument: 1-based
    const ast::FunctionDecl* function = nullptr; // Argument: the callee, if called directly
};

/// The builtin functions whose calls sema types specially instead of through a signature (builtins.cpp).
enum class GenericBuiltin : std::uint8_t {
    None,
    Overflow,      // __builtin_add_overflow(a, b, &r) and friends: any integer types
    ConstantP,     // __builtin_constant_p(x): the operand is not evaluated
    FloatClassify, // __builtin_isnan(x) and friends: any real floating type, not promoted
    Fpclassify,    // __builtin_fpclassify(nan, infinite, normal, subnormal, zero, x): x is not promoted
    FloatCompare,  // __builtin_isgreater(x, y) and friends
    VaStart,       // __builtin_va_start(ap, last): ap stays an lvalue
    VaEnd,         // __builtin_va_end(ap): ap stays an lvalue
    VaCopy,        // __builtin_va_copy(destination, source): the destination stays an lvalue
    Atomic,        // __atomic_load_n(p, order), __sync_fetch_and_add(p, v) and friends: typed by what p points to
};

/// What the builtin table knows about a function name.
struct BuiltinInfo {
    GenericBuiltin generic = GenericBuiltin::None;
    ast::QualType signature; // null for generic builtins
    bool isLibraryFunction = false;
    bool isNoreturn = false;
};

/// The table entry for a name: `__builtin_x`, a C library function `x` that GCC knows as a builtin, or a GNU builtin
/// with a name of its own (`__atomic_x`, `__sync_x`).
[[nodiscard]] std::optional<BuiltinInfo> lookupBuiltin(ast::AstContext& context, std::string_view name);

/// The parameters of a type-generic atomic builtin (GenericBuiltin::Atomic), one letter each: 'P' the pointer to the
/// atomic object (a T *), 'p' another pointer to a T, 'v' a value converted to T, 'b' the `weak` flag, 'o' a memory
/// order; a final '.' accepts further arguments (the variables of the __sync builtins, ignored).
struct AtomicSignature {
    enum class Form : std::uint8_t {
        Generic,    // __atomic_load(p, &result, order): T is any complete object type
        Sized,      // __atomic_load_n(p, order): T is an integer or pointer type of 1, 2, 4, 8 or 16 bytes
        Arithmetic, // __atomic_fetch_add(p, v, order): Sized, not _Bool; on pointers in bytes, not elements
    };
    enum class Result : std::uint8_t { Operand, Void, Bool };

    std::string_view parameters;
    Form form;
    Result result;
};
[[nodiscard]] std::optional<AtomicSignature> atomicSignature(std::string_view name);

class Sema {
public:
    Sema(ast::AstContext& context, DiagnosticEngine& diagnostics);

    /// Throws SemaError after reporting the first error.
    void analyzeTranslationUnit(ast::TranslationUnit& unit);
    /// A witness constraint (sema/expression.hpp) as a scalar rvalue; throws SemaError after reporting an error.
    [[nodiscard]] ast::Expr* analyzeConstraint(ast::Expr* expr, ast::QualType resultType);

private:
    struct FunctionContext {
        ast::FunctionDecl* function = nullptr;
        ast::QualType returnType;
        std::vector<ast::VarDecl*> locals;
    };

    // ---- sema.cpp: diagnostics and the translation unit ----
    [[noreturn]] void error(SourceLocation location, std::string message);
    void warning(SourceLocation location, std::string message);
    [[nodiscard]] std::string typeName(ast::QualType type) const;
    void completeTentativeDefinitions();

    // ---- declarations.cpp ----
    void declaration(ast::Decl* decl);
    void varDecl(ast::VarDecl* var);
    void functionDecl(ast::FunctionDecl* function);
    void typedefDecl(ast::TypedefDecl* typedefDecl);
    void staticAssert(ast::StaticAssertDecl* staticAssert);
    void parameter(ast::VarDecl* parameter);
    [[nodiscard]] ast::QualType adjustedParameterType(ast::QualType type);
    void adjustParameterTypes(ast::FunctionDecl* function);
    void applyParameterModes(ast::FunctionDecl* function);
    void mergeVariable(ast::VarDecl* var);
    void mergeFunction(ast::FunctionDecl* function);
    bool mergeOldStyleDefinition(ast::FunctionDecl* function, const ast::FunctionDecl* previous);
    void applyBuiltinSignature(ast::FunctionDecl* function);
    /// Resolves the array sizes and typeofs a type refers to, without entering records (C17 6.7.6.2).
    void resolveType(ast::QualType type, SourceLocation location);
    void resolveArraySize(const ast::ArrayType* array, SourceLocation location);
    /// The alignment the `aligned` attributes and _Alignas specifiers of a declaration request: a power of two (GCC's
    /// biggest alignment for `aligned` without argument), or 0 if they request none.
    [[nodiscard]] std::uint64_t requestedAlignment(const ast::Decl* decl);
    /// The type that the `mode` attribute gives an integer type, or nullopt if the attribute is absent.
    [[nodiscard]] std::optional<ast::QualType> modeType(const ast::Decl* decl, ast::QualType type);
    /// Rejects attributes that change the meaning of a program in ways NITWIT does not model.
    void checkUnsupportedAttributes(const ast::Decl* decl);
    [[nodiscard]] static const ast::Attribute* findAttribute(const ast::Decl* decl, std::string_view name);

    // ---- records.cpp ----
    /// Analyzes the members of a complete struct or union and computes its layout; idempotent.
    void completeRecord(ast::RecordDecl* record);
    /// Computes the values and the integer type of an enumeration; idempotent.
    void completeEnum(ast::EnumDecl* enumDecl, SourceLocation use);
    void field(ast::FieldDecl* field, bool isLast);
    void enumConstant(ast::EnumConstantDecl* constant, const ast::EnumConstantDecl* previous);
    /// Requires a complete object type (C17 6.2.5p1); completes records and enumerations on the way.
    void requireComplete(ast::QualType type, SourceLocation location, std::string_view what);
    [[nodiscard]] bool isComplete(ast::QualType type);
    /// The field `name` of a record, with the anonymous members leading to it first; empty if there is none.
    [[nodiscard]] std::vector<ast::FieldDecl*> findField(ast::RecordDecl* record, Symbol name);

    // ---- layout.cpp ----
    [[nodiscard]] const ast::RecordLayout* computeLayout(const ast::RecordDecl* record);

    // ---- expressions.cpp ----
    /// Analyzes an expression in place: types, value categories and the conversions of its operands. The expression
    /// itself is not converted; its user does that. Analyzing an analyzed expression has no effect.
    void expr(ast::Expr* expr);
    void integerLiteral(ast::IntegerLiteralExpr* literal);
    void declRef(ast::DeclRefExpr* reference);
    void call(ast::CallExpr* call);
    void genericBuiltinCall(ast::CallExpr* call, GenericBuiltin builtin);
    void atomicCall(ast::CallExpr* call, std::string_view name); // builtins.cpp
    void member(ast::MemberExpr* member);
    void subscript(ast::ArraySubscriptExpr* subscript);
    void explicitCast(ast::CastExpr* castExpr);
    void compoundLiteral(ast::CompoundLiteralExpr* literal);
    void sizeOfAlignOf(ast::SizeOfAlignOfExpr* sizeOf);
    void offsetOf(ast::OffsetOfExpr* offsetOf);
    void vaArg(ast::VaArgExpr* vaArg);
    void statementExpr(ast::StatementExpr* statementExpr);
    void genericSelection(ast::GenericSelectionExpr* generic);
    void chooseExpr(ast::ChooseExpr* choose);
    /// The value of an integer constant expression, or an error with `what` in the message.
    [[nodiscard]] Int128 integerConstant(ast::Expr* expr, std::string_view what);

    // ---- operators.cpp ----
    void unary(ast::UnaryExpr* unary);
    void binary(ast::BinaryExpr* binary);
    void binaryOperator(ast::BinaryExpr* binary);
    [[noreturn]] void invalidOperands(const ast::BinaryExpr* binary);
    void checkPointerArithmetic(ast::QualType pointer, SourceLocation location);
    [[nodiscard]] ast::Expr* implicitPointerConversion(ast::Expr* value, const ast::Expr* original, ast::QualType type);
    void arithmetic(ast::BinaryExpr* binary);
    void additive(ast::BinaryExpr* binary);
    void shift(ast::BinaryExpr* binary);
    void comparison(ast::BinaryExpr* binary);
    void logical(ast::BinaryExpr* binary);
    void assignment(ast::BinaryExpr* binary);
    void compoundAssignment(ast::BinaryExpr* binary);
    void conditional(ast::ConditionalExpr* conditional);
    void requireModifiableLvalue(const ast::Expr* expr, SourceLocation location, std::string_view operation);
    void rejectComplex(const ast::Expr* expr, SourceLocation location);

    // ---- conversions.cpp ----
    [[nodiscard]] ast::Expr* implicitCast(ast::Expr* expr, ast::QualType type, ast::CastKind kind);
    /// Lvalue conversion, array and function decay (C17 6.3.2.1).
    [[nodiscard]] ast::Expr* rvalue(ast::Expr* expr);
    /// The type of an expression after rvalue().
    [[nodiscard]] ast::QualType valueType(const ast::Expr* expr);
    /// rvalue() followed by the integer promotions, which GCC applies to bit-fields by their width.
    [[nodiscard]] ast::Expr* promote(ast::Expr* expr);
    [[nodiscard]] ast::QualType promotedTypeOf(const ast::Expr* expr);
    /// rvalue() followed by the default argument promotions (C17 6.5.2.2p6).
    [[nodiscard]] ast::Expr* defaultArgumentPromotion(ast::Expr* expr);
    /// Converts an rvalue between scalar types, or to void; no diagnostics.
    [[nodiscard]] ast::Expr* convert(ast::Expr* expr, ast::QualType type);
    [[nodiscard]] ast::CastKind castKind(const ast::Expr* expr, ast::QualType from, ast::QualType to);
    /// The usual arithmetic conversions (C17 6.3.1.8) of two analyzed operands; returns their common type.
    ast::QualType usualArithmeticConversions(ast::Expr*& lhs, ast::Expr*& rhs);
    /// Converts an analyzed expression as if by assignment to `type` (C17 6.5.16.1), with GCC's diagnostics.
    [[nodiscard]] ast::Expr* convertForAssignment(ast::Expr* expr, ast::QualType type,
                                                  const AssignmentContext& context);
    struct ConversionWording {
        std::string subject;              // "assignment to 'T' from 'S'", for "... makes integer from pointer ..."
        std::string action;               // "assignment", for "... discards 'const' qualifier ..."
        std::string incompatiblePointers; // the whole warning
        std::string incompatibleTypes;    // the whole error
    };
    [[nodiscard]] ConversionWording conversionWording(ast::QualType from, ast::QualType to,
                                                      const AssignmentContext& context);
    [[nodiscard]] ast::Expr* transparentUnionArgument(ast::Expr* expr, ast::QualType target);
    /// C17 6.3.2.3p3: an integer constant expression with the value 0, or one cast to void *.
    [[nodiscard]] bool isNullPointerConstant(const ast::Expr* expr);
    [[nodiscard]] bool isVaListAddress(ast::QualType type);
    [[nodiscard]] bool isTransparentUnion(ast::QualType type);
    /// rvalue(), and for the address of a va_list (see isVaListAddress) the va_list it points to.
    [[nodiscard]] ast::Expr* vaListValue(ast::Expr* expr);
    /// An rvalue of scalar type for a condition (C17 6.8.4.1p1, 6.8.5p2, 6.5.13-15).
    [[nodiscard]] ast::Expr* condition(ast::Expr* expr, std::string_view what);

    // ---- initializers.cpp ----
    /// Converts an initializer to its semantic form for an object of `type` (C17 6.7.9). An incomplete array type is
    /// completed from the initializer and returned through `type`. Only the initializer of an object with static
    /// storage duration may initialize a flexible array member (GCC).
    [[nodiscard]] ast::Expr* initializer(ast::QualType& type, ast::Expr* init, bool isStatic);

    // ---- statements.cpp ----
    void functionBody(ast::FunctionDecl* function);
    void stmt(ast::Stmt* stmt);
    void switchStmt(ast::SwitchStmt* switchStmt);
    void returnStmt(ast::ReturnStmt* returnStmt);

    ast::AstContext& context_;
    DiagnosticEngine& diagnostics_;
    std::optional<FunctionContext> function_;
    /// The type of `\result` while a constraint is analyzed; null otherwise.
    ast::QualType resultType_;
    /// > 0 while the types of a function's parameters are resolved: variable length arrays are allowed there.
    unsigned prototypeDepth_ = 0;
    std::unordered_set<const ast::Type*> resolvedTypes_;
    std::unordered_set<const ast::Decl*> completing_; // records and enumerations being completed, to find cycles
    std::vector<ast::VarDecl*> fileScopeVariables_;

    friend class InitializerBuilder;
};

} // namespace nitwit::sema::detail
