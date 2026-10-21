// Integer constant expressions (C17 6.6), evaluated on the analyzed AST with the arithmetic of the value module.
//
// Besides C's integer constant expressions, this folds what GCC folds in their place: floating constants and their
// arithmetic under a cast to an integer type, and addresses computed from null pointer constants, as in the classic
// `offsetof` macro `(size_t)&((struct S *)0)->member`. Any undefined behaviour (signed overflow, division by zero,
// out-of-range conversions and shift counts) makes an expression non-constant (C17 6.6p4). Left shifts of negative
// values and into the sign bit are the exception: GCC folds them without complaint, to the value x86 computes.

#include "sema/constant.hpp"

#include <bit>
#include <vector>

#include "ast/casting.hpp"
#include "sema/type_rules.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"
#include "value/arithmetic.hpp"

namespace nitwit::sema {

namespace {

using detail::classify;
using detail::TypeClass;
using value::Value;

class Evaluator {
public:
    explicit Evaluator(ast::AstContext& context) : context_(context) {}

    std::optional<Value> value(const ast::Expr* expr);

private:
    std::optional<Value> leaf(const ast::Expr* expr);
    std::optional<Value> binary(const ast::BinaryExpr* binary, const Value& lhs);
    std::optional<Value> pointerArithmetic(const ast::BinaryExpr* binary, const Value& lhs, const Value& rhs);
    std::optional<Value> unary(const ast::UnaryExpr* unary);
    std::optional<Value> convert(ast::CastKind kind, const Value& operand, ast::QualType from, ast::QualType to);
    std::optional<Value> call(const ast::CallExpr* call);
    std::optional<Value> sizeOrAlignment(const ast::SizeOfAlignOfExpr* sizeOf);
    std::optional<Value> offsetOf(const ast::OffsetOfExpr* offsetOf);
    std::optional<UInt128> address(const ast::Expr* expr);
    std::optional<std::uint64_t> elementSize(ast::QualType type);

    [[nodiscard]] value::IntegerFormat format(ast::QualType type) { return integerFormat(context_, type); }
    [[nodiscard]] Value integer(UInt128 bits, ast::QualType type) { return Value::integer(bits, format(type)); }
    [[nodiscard]] static bool isTrue(const Value& value) { return value::toBool(value).bits() != 0; }

    ast::AstContext& context_;
};

bool isValueConversion(ast::CastKind kind) {
    return kind != ast::CastKind::LValueToRValue && kind != ast::CastKind::ArrayToPointerDecay
           && kind != ast::CastKind::FunctionToPointerDecay;
}

// The operands of binary operators and conversions are evaluated first. Their left-nested chains (`a + b + c + ...`)
// are walked in a loop, so that the recursion depth does not grow with the length of a chain.
std::optional<Value> Evaluator::value(const ast::Expr* expr) {
    std::vector<const ast::Expr*> pending;
    while (true) {
        if (const auto* paren = ast::dyn_cast<ast::ParenExpr>(expr)) {
            expr = paren->inner();
        } else if (const auto* binary = ast::dyn_cast<ast::BinaryExpr>(expr)) {
            pending.push_back(binary);
            expr = binary->lhs();
        } else if (const auto* cast = ast::dyn_cast<ast::ImplicitCastExpr>(expr);
                   cast != nullptr && isValueConversion(cast->castKind())) {
            pending.push_back(cast);
            expr = cast->operand();
        } else {
            break;
        }
    }
    std::optional<Value> result = leaf(expr);
    for (auto node = pending.rbegin(); node != pending.rend() && result; ++node) {
        if (const auto* cast = ast::dyn_cast<ast::ImplicitCastExpr>(*node)) {
            result = convert(cast->castKind(), *result, cast->operand()->type(), cast->type());
        } else {
            result = binary(ast::cast<ast::BinaryExpr>(*node), *result);
        }
    }
    return result;
}

std::optional<Value> Evaluator::leaf(const ast::Expr* expr) {
    switch (expr->kind()) {
    case ast::ExprKind::IntegerLiteral:
        return integer(ast::cast<ast::IntegerLiteralExpr>(expr)->value().value, expr->type());
    case ast::ExprKind::CharacterLiteral:
        return integer(
            static_cast<UInt128>(static_cast<Int128>(ast::cast<ast::CharacterLiteralExpr>(expr)->value().value)),
            expr->type());
    case ast::ExprKind::FloatingLiteral: {
        const lex::FloatingLiteral& literal = ast::cast<ast::FloatingLiteralExpr>(expr)->value();
        switch (floatFormat(context_, expr->type())) {
        case value::FloatFormat::Float:
            return Value::ofFloat(literal.floatValue);
        case value::FloatFormat::Double:
            return Value::ofDouble(literal.doubleValue);
        case value::FloatFormat::LongDouble:
            return Value::ofLongDouble(literal.longDoubleValue);
        }
        NITWIT_UNREACHABLE("unknown float format");
    }
    case ast::ExprKind::DeclRef:
        if (const auto* constant = ast::dyn_cast<ast::EnumConstantDecl>(ast::cast<ast::DeclRefExpr>(expr)->decl())) {
            return integer(static_cast<UInt128>(constant->value()), expr->type());
        }
        return std::nullopt;
    case ast::ExprKind::Unary:
        return unary(ast::cast<ast::UnaryExpr>(expr));
    case ast::ExprKind::Conditional: {
        const auto* conditional = ast::cast<ast::ConditionalExpr>(expr);
        const std::optional<Value> condition = value(conditional->condition());
        if (!condition) {
            return std::nullopt;
        }
        if (isTrue(*condition)) {
            // GNU `c ?: b`: the condition, converted by sema to the result type, is the value.
            return conditional->trueExpr() != nullptr ? value(conditional->trueExpr()) : condition;
        }
        return value(conditional->falseExpr());
    }
    case ast::ExprKind::Cast: {
        const auto* castExpr = ast::cast<ast::CastExpr>(expr);
        if (castExpr->castKind() == ast::CastKind::ToVoid || castExpr->castKind() == ast::CastKind::ToUnion) {
            return std::nullopt;
        }
        const std::optional<Value> operand = value(castExpr->operand());
        if (!operand) {
            return std::nullopt;
        }
        return convert(castExpr->castKind(), *operand, castExpr->operand()->type(), castExpr->type());
    }
    case ast::ExprKind::ImplicitCast: {
        const auto* cast = ast::cast<ast::ImplicitCastExpr>(expr);
        if (cast->castKind() != ast::CastKind::ArrayToPointerDecay) {
            return std::nullopt; // reading an object, or the address of a function
        }
        const std::optional<UInt128> array = address(cast->operand());
        return array ? std::optional(integer(*array, expr->type())) : std::nullopt;
    }
    case ast::ExprKind::Call:
        return call(ast::cast<ast::CallExpr>(expr));
    case ast::ExprKind::SizeOfAlignOf:
        return sizeOrAlignment(ast::cast<ast::SizeOfAlignOfExpr>(expr));
    case ast::ExprKind::OffsetOf:
        return offsetOf(ast::cast<ast::OffsetOfExpr>(expr));
    case ast::ExprKind::TypesCompatible: {
        // GCC ignores the top-level qualifiers.
        const auto* compatible = ast::cast<ast::TypesCompatibleExpr>(expr);
        return integer(detail::compatibleUnqualified(context_, compatible->first(), compatible->second()) ? 1 : 0,
                       expr->type());
    }
    case ast::ExprKind::ChooseExpr:
        return value(ast::cast<ast::ChooseExpr>(expr)->selected());
    case ast::ExprKind::GenericSelection:
        return value(ast::cast<ast::GenericSelectionExpr>(expr)->selected());
    default:
        return std::nullopt;
    }
}

std::optional<Value> Evaluator::binary(const ast::BinaryExpr* binary, const Value& lhs) {
    const ast::QualType type = binary->type();
    switch (binary->op()) {
    case ast::BinaryOperator::LogicalAnd:
    case ast::BinaryOperator::LogicalOr: {
        // C17 6.6p3: an operand that is not evaluated need not be constant.
        const bool isAnd = binary->op() == ast::BinaryOperator::LogicalAnd;
        if (isTrue(lhs) != isAnd) {
            return integer(isAnd ? 0 : 1, type);
        }
        const std::optional<Value> rhs = value(binary->rhs());
        return rhs ? std::optional(integer(isTrue(*rhs) ? 1 : 0, type)) : std::nullopt;
    }
    case ast::BinaryOperator::Comma:
        return std::nullopt; // C17 6.6p3
    default:
        break;
    }
    if (binary->op() >= ast::BinaryOperator::Assign) {
        return std::nullopt;
    }
    const std::optional<Value> rhs = value(binary->rhs());
    if (!rhs) {
        return std::nullopt;
    }
    const ast::QualType operandType = binary->lhs()->type();
    const TypeClass operandClass = classify(context_, operandType);
    const auto compare = [&](value::Comparison comparison) {
        const bool result = operandClass == TypeClass::Floating
                                ? value::floatCompare(comparison, lhs, *rhs, floatFormat(context_, operandType))
                                : value::integerCompare(comparison, lhs, *rhs, format(operandType));
        return integer(result ? 1 : 0, type);
    };
    switch (binary->op()) {
    case ast::BinaryOperator::Less:
        return compare(value::Comparison::Less);
    case ast::BinaryOperator::Greater:
        return compare(value::Comparison::Greater);
    case ast::BinaryOperator::LessEqual:
        return compare(value::Comparison::LessEqual);
    case ast::BinaryOperator::GreaterEqual:
        return compare(value::Comparison::GreaterEqual);
    case ast::BinaryOperator::Equal:
        return compare(value::Comparison::Equal);
    case ast::BinaryOperator::NotEqual:
        return compare(value::Comparison::NotEqual);
    case ast::BinaryOperator::Shl:
    case ast::BinaryOperator::Shr: {
        const value::Result result = value::shift(
            binary->op() == ast::BinaryOperator::Shl ? value::ShiftOperator::Left : value::ShiftOperator::Right, lhs,
            format(operandType), *rhs, format(binary->rhs()->type()));
        if (result.undefinedBehavior == value::UndefinedBehavior::ShiftCountOutOfRange) {
            return std::nullopt;
        }
        return result.value;
    }
    default:
        break;
    }
    if (classify(context_, type) == TypeClass::Pointer || classify(context_, operandType) == TypeClass::Pointer
        || classify(context_, binary->rhs()->type()) == TypeClass::Pointer) {
        return pointerArithmetic(binary, lhs, *rhs);
    }
    if (classify(context_, type) == TypeClass::Floating) {
        value::FloatOperator op = value::FloatOperator::Add;
        switch (binary->op()) {
        case ast::BinaryOperator::Add:
            op = value::FloatOperator::Add;
            break;
        case ast::BinaryOperator::Sub:
            op = value::FloatOperator::Sub;
            break;
        case ast::BinaryOperator::Mul:
            op = value::FloatOperator::Mul;
            break;
        case ast::BinaryOperator::Div:
            op = value::FloatOperator::Div;
            break;
        default:
            return std::nullopt;
        }
        return value::floatBinary(op, lhs, *rhs, floatFormat(context_, type));
    }
    value::IntegerOperator op = value::IntegerOperator::Add;
    switch (binary->op()) {
    case ast::BinaryOperator::Add:
        op = value::IntegerOperator::Add;
        break;
    case ast::BinaryOperator::Sub:
        op = value::IntegerOperator::Sub;
        break;
    case ast::BinaryOperator::Mul:
        op = value::IntegerOperator::Mul;
        break;
    case ast::BinaryOperator::Div:
        op = value::IntegerOperator::Div;
        break;
    case ast::BinaryOperator::Rem:
        op = value::IntegerOperator::Rem;
        break;
    case ast::BinaryOperator::BitwiseAnd:
        op = value::IntegerOperator::And;
        break;
    case ast::BinaryOperator::BitwiseOr:
        op = value::IntegerOperator::Or;
        break;
    case ast::BinaryOperator::BitwiseXor:
        op = value::IntegerOperator::Xor;
        break;
    default:
        return std::nullopt;
    }
    const value::Result result = value::integerBinary(op, lhs, *rhs, format(type));
    if (result.undefinedBehavior != value::UndefinedBehavior::None) {
        return std::nullopt;
    }
    return result.value;
}

std::optional<std::uint64_t> Evaluator::elementSize(ast::QualType type) {
    const TypeClass typeClass = classify(context_, type);
    if (typeClass == TypeClass::Void || typeClass == TypeClass::Function) {
        return 1; // GNU
    }
    if (detail::isVariablySized(type)) {
        return std::nullopt;
    }
    return sizeOf(context_, type);
}

// Pointer arithmetic on addresses known as numbers, i.e. computed from null pointer constants.
std::optional<Value> Evaluator::pointerArithmetic(const ast::BinaryExpr* binary, const Value& lhs, const Value& rhs) {
    const ast::QualType lhsType = binary->lhs()->type();
    const ast::QualType rhsType = binary->rhs()->type();
    const bool lhsIsPointer = classify(context_, lhsType) == TypeClass::Pointer;
    const bool rhsIsPointer = classify(context_, rhsType) == TypeClass::Pointer;
    const ast::QualType pointerType = lhsIsPointer ? lhsType : rhsType;
    const std::optional<std::uint64_t> size = elementSize(detail::pointeeOf(pointerType));
    if (!size) {
        return std::nullopt;
    }
    if (lhsIsPointer && rhsIsPointer) {
        if (*size == 0) {
            return std::nullopt;
        }
        const value::IntegerFormat pointerFormat = format(lhsType);
        const Value difference = Value::integer(lhs.bits() - rhs.bits(), {pointerFormat.bits, true});
        return integer(static_cast<UInt128>(difference.signedBits() / static_cast<Int128>(*size)), binary->type());
    }
    const Value& pointer = lhsIsPointer ? lhs : rhs;
    const Value& index = lhsIsPointer ? rhs : lhs;
    const UInt128 offset = static_cast<UInt128>(index.signedBits()) * *size;
    const UInt128 bits = binary->op() == ast::BinaryOperator::Sub ? pointer.bits() - offset : pointer.bits() + offset;
    return integer(bits, binary->type());
}

std::optional<Value> Evaluator::unary(const ast::UnaryExpr* unary) {
    const ast::QualType type = unary->type();
    switch (unary->op()) {
    case ast::UnaryOperator::Plus:
    case ast::UnaryOperator::Extension:
        return value(unary->operand());
    case ast::UnaryOperator::AddressOf: {
        const std::optional<UInt128> address = this->address(unary->operand());
        return address ? std::optional(integer(*address, type)) : std::nullopt;
    }
    default:
        break;
    }
    const std::optional<Value> operand = value(unary->operand());
    if (!operand) {
        return std::nullopt;
    }
    switch (unary->op()) {
    case ast::UnaryOperator::Minus: {
        if (classify(context_, type) == TypeClass::Floating) {
            return value::floatNegate(*operand, floatFormat(context_, type));
        }
        const value::Result result = value::integerNegate(*operand, format(type));
        if (result.undefinedBehavior != value::UndefinedBehavior::None) {
            return std::nullopt;
        }
        return result.value;
    }
    case ast::UnaryOperator::BitwiseNot:
        return value::integerComplement(*operand, format(type));
    case ast::UnaryOperator::LogicalNot:
        return integer(isTrue(*operand) ? 0 : 1, type);
    default:
        return std::nullopt;
    }
}

std::optional<Value> Evaluator::convert(ast::CastKind kind, const Value& operand, ast::QualType from,
                                        ast::QualType to) {
    switch (kind) {
    case ast::CastKind::NoOp:
    case ast::CastKind::BitCast:
        return operand;
    case ast::CastKind::IntegralCast:
    case ast::CastKind::IntegralToPointer:
    case ast::CastKind::PointerToIntegral:
        return value::convertInteger(operand, format(to));
    case ast::CastKind::NullToPointer:
        return integer(0, to);
    case ast::CastKind::IntegralToBoolean:
    case ast::CastKind::FloatingToBoolean:
    case ast::CastKind::PointerToBoolean:
        return value::toBool(operand);
    case ast::CastKind::IntegralToFloating:
        return value::integerToFloat(operand, format(from), floatFormat(context_, to));
    case ast::CastKind::FloatingToIntegral: {
        const value::Result result =
            value::floatToInteger(operand, floatFormat(context_, from), format(to), context_.target().dataModel());
        if (result.undefinedBehavior != value::UndefinedBehavior::None) {
            return std::nullopt;
        }
        return result.value;
    }
    case ast::CastKind::FloatingCast:
        return value::convertFloat(operand, floatFormat(context_, from), floatFormat(context_, to));
    case ast::CastKind::LValueToRValue:
    case ast::CastKind::ArrayToPointerDecay:
    case ast::CastKind::FunctionToPointerDecay:
    case ast::CastKind::ToVoid:
    case ast::CastKind::ToUnion:
        return std::nullopt;
    }
    NITWIT_UNREACHABLE("unknown cast kind");
}

// GCC folds some builtins with constant arguments.
std::optional<Value> Evaluator::call(const ast::CallExpr* call) {
    const ast::FunctionDecl* function = call->directCallee();
    if (function == nullptr || !function->name().text().starts_with("__builtin_")) {
        return std::nullopt;
    }
    const std::string_view name = function->name().text().substr(std::string_view("__builtin_").size());
    const std::span<ast::Expr*> arguments = call->arguments();
    const ast::QualType type = call->type();
    if (name == "constant_p") {
        return integer(arguments.size() == 1 && value(arguments[0]) ? 1 : 0, type);
    }
    if (arguments.empty()) {
        return std::nullopt;
    }
    const std::optional<Value> argument = value(arguments[0]);
    if (!argument) {
        return std::nullopt;
    }
    if (name == "expect" || name == "expect_with_probability") {
        return argument;
    }
    const value::IntegerFormat argumentFormat = format(arguments[0]->type());
    const UInt128 mask = argumentFormat.bits == 128 ? ~UInt128{0} : (UInt128{1} << argumentFormat.bits) - 1;
    const UInt128 bits = argument->bits() & mask;
    const auto count = [&](UInt128 n) {
        return integer(n, type);
    };
    if (name.starts_with("bswap")) {
        UInt128 swapped = 0;
        for (unsigned byte = 0; byte < argumentFormat.bits / 8u; ++byte) {
            swapped = (swapped << 8) | ((bits >> (8 * byte)) & 0xff);
        }
        return count(swapped);
    }
    const auto lowBits = static_cast<std::uint64_t>(bits);
    if (argumentFormat.bits > 64) {
        return std::nullopt;
    }
    if (name.starts_with("clz")) {
        return lowBits == 0 ? std::nullopt
                            : std::optional(
                                  count(static_cast<unsigned>(std::countl_zero(lowBits)) - (64 - argumentFormat.bits)));
    }
    if (name.starts_with("ctz")) {
        return lowBits == 0 ? std::nullopt : std::optional(count(static_cast<unsigned>(std::countr_zero(lowBits))));
    }
    if (name.starts_with("popcount")) {
        return count(static_cast<unsigned>(std::popcount(lowBits)));
    }
    if (name.starts_with("parity")) {
        return count(static_cast<unsigned>(std::popcount(lowBits) & 1));
    }
    if (name.starts_with("ffs")) {
        return count(lowBits == 0 ? 0 : static_cast<unsigned>(std::countr_zero(lowBits)) + 1);
    }
    return std::nullopt;
}

// sizeof, _Alignof and __alignof__ as GCC folds them; for an expression, __alignof__ and _Alignof give the alignment
// of the declaration or member it names (GCC's c_alignof_expr).
std::optional<Value> Evaluator::sizeOrAlignment(const ast::SizeOfAlignOfExpr* sizeOf) {
    const ast::Expr* operand = sizeOf->operand();
    const ast::QualType type = operand != nullptr ? operand->type() : sizeOf->typeOperand();
    const TypeClass typeClass = classify(context_, type);
    std::uint64_t result = 1; // GNU: void and functions have size and alignment 1
    if (sizeOf->op() == ast::SizeOfAlignOfExpr::Operator::SizeOf) {
        if (typeClass != TypeClass::Void && typeClass != TypeClass::Function) {
            if (detail::isVariablySized(type)) {
                return std::nullopt;
            }
            result = sema::sizeOf(context_, type);
        }
    } else if (operand == nullptr) {
        if (typeClass != TypeClass::Void && typeClass != TypeClass::Function) {
            result = sizeOf->op() == ast::SizeOfAlignOfExpr::Operator::AlignOf
                         ? alignOf(context_, type)
                         : detail::preferredAlignment(context_, type);
        }
    } else {
        const ast::Expr* named = ast::ignoreParens(operand);
        if (const auto* reference = ast::dyn_cast<ast::DeclRefExpr>(named)) {
            if (const auto* var = ast::dyn_cast<ast::VarDecl>(reference->decl())) {
                result = std::max(detail::preferredAlignment(context_, var->type()), var->requestedAlignment());
            }
        } else if (const auto* member = ast::dyn_cast<ast::MemberExpr>(named)) {
            result = detail::fieldAlignment(context_, member->field());
        } else if (const auto* unary = ast::dyn_cast<ast::UnaryExpr>(named);
                   unary != nullptr && unary->op() == ast::UnaryOperator::Dereference) {
            result = detail::preferredAlignment(context_, type);
        } else if (typeClass != TypeClass::Void && typeClass != TypeClass::Function) {
            result = detail::preferredAlignment(context_, type);
        }
    }
    return integer(result, sizeOf->type());
}

std::optional<Value> Evaluator::offsetOf(const ast::OffsetOfExpr* offsetOf) {
    UInt128 offset = 0;
    ast::QualType current = offsetOf->recordType();
    for (const ast::OffsetOfExpr::Component& component : offsetOf->components()) {
        if (component.isField) {
            const std::optional<std::uint64_t> field = detail::fieldOffset(detail::recordOf(current), component.field);
            if (!field) {
                return std::nullopt;
            }
            offset += *field;
            current = component.field->type();
            continue;
        }
        const std::optional<Value> index = value(component.index);
        const ast::QualType element = detail::arrayOf(current)->element();
        const std::optional<std::uint64_t> size = elementSize(element);
        if (!index || !size) {
            return std::nullopt;
        }
        offset += static_cast<UInt128>(index->signedBits()) * *size;
        current = element;
    }
    return integer(offset, offsetOf->type());
}

// The address of an lvalue whose address is a number: a member or element reached from a null-based pointer.
std::optional<UInt128> Evaluator::address(const ast::Expr* expr) {
    expr = ast::ignoreParens(expr);
    if (const auto* unary = ast::dyn_cast<ast::UnaryExpr>(expr)) {
        if (unary->op() != ast::UnaryOperator::Dereference) {
            return std::nullopt;
        }
        const std::optional<Value> pointer = value(unary->operand());
        return pointer ? std::optional(pointer->bits()) : std::nullopt;
    }
    if (const auto* member = ast::dyn_cast<ast::MemberExpr>(expr)) {
        const ast::FieldDecl* field = member->field();
        if (field->isBitField()) {
            return std::nullopt;
        }
        std::optional<UInt128> base;
        if (member->isArrow()) {
            const std::optional<Value> pointer = value(member->base());
            base = pointer ? std::optional(pointer->bits()) : std::nullopt;
        } else {
            base = address(member->base());
        }
        if (!base) {
            return std::nullopt;
        }
        return *base + layoutOf(field->parent()).fieldOffsets[field->index()];
    }
    if (const auto* subscript = ast::dyn_cast<ast::ArraySubscriptExpr>(expr)) {
        const std::optional<Value> base = value(subscript->base());
        const std::optional<Value> index = value(subscript->index());
        const std::optional<std::uint64_t> size = elementSize(expr->type());
        if (!base || !index || !size) {
            return std::nullopt;
        }
        return base->bits() + static_cast<UInt128>(index->signedBits()) * *size;
    }
    return std::nullopt;
}

} // namespace

std::optional<Int128> evaluateIntegerConstant(ast::AstContext& context, const ast::Expr* expr) {
    if (expr->type().isNull() || !isInteger(context, expr->type())) {
        return std::nullopt;
    }
    Evaluator evaluator(context);
    const std::optional<Value> result = evaluator.value(expr);
    if (!result || result->kind() != Value::Kind::Integer) {
        return std::nullopt;
    }
    return result->signedBits();
}

} // namespace nitwit::sema
