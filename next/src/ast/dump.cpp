#include "ast/dump.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "ast/casting.hpp"
#include "support/assert.hpp"
#include "support/int128.hpp"

namespace nitwit::ast {

namespace {

std::string_view builtinName(BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Void:
        return "void";
    case BuiltinKind::Bool:
        return "_Bool";
    case BuiltinKind::Char:
        return "char";
    case BuiltinKind::SignedChar:
        return "signed char";
    case BuiltinKind::UnsignedChar:
        return "unsigned char";
    case BuiltinKind::Short:
        return "short";
    case BuiltinKind::UnsignedShort:
        return "unsigned short";
    case BuiltinKind::Int:
        return "int";
    case BuiltinKind::UnsignedInt:
        return "unsigned int";
    case BuiltinKind::Long:
        return "long";
    case BuiltinKind::UnsignedLong:
        return "unsigned long";
    case BuiltinKind::LongLong:
        return "long long";
    case BuiltinKind::UnsignedLongLong:
        return "unsigned long long";
    case BuiltinKind::Int128:
        return "__int128";
    case BuiltinKind::UnsignedInt128:
        return "unsigned __int128";
    case BuiltinKind::Float:
        return "float";
    case BuiltinKind::Double:
        return "double";
    case BuiltinKind::LongDouble:
        return "long double";
    case BuiltinKind::VaList:
        return "__builtin_va_list";
    }
    NITWIT_UNREACHABLE("unknown builtin kind");
}

std::string_view unaryOperatorName(UnaryOperator op) {
    switch (op) {
    case UnaryOperator::PostIncrement:
    case UnaryOperator::PreIncrement:
        return "++";
    case UnaryOperator::PostDecrement:
    case UnaryOperator::PreDecrement:
        return "--";
    case UnaryOperator::AddressOf:
        return "&";
    case UnaryOperator::Dereference:
        return "*";
    case UnaryOperator::Plus:
        return "+";
    case UnaryOperator::Minus:
        return "-";
    case UnaryOperator::BitwiseNot:
        return "~";
    case UnaryOperator::LogicalNot:
        return "!";
    case UnaryOperator::Real:
        return "__real__";
    case UnaryOperator::Imag:
        return "__imag__";
    case UnaryOperator::Extension:
        return "__extension__";
    }
    NITWIT_UNREACHABLE("unknown unary operator");
}

bool isPostfix(UnaryOperator op) {
    return op == UnaryOperator::PostIncrement || op == UnaryOperator::PostDecrement;
}

std::string_view binaryOperatorName(BinaryOperator op) {
    switch (op) {
    case BinaryOperator::Mul:
        return "*";
    case BinaryOperator::Div:
        return "/";
    case BinaryOperator::Rem:
        return "%";
    case BinaryOperator::Add:
        return "+";
    case BinaryOperator::Sub:
        return "-";
    case BinaryOperator::Shl:
        return "<<";
    case BinaryOperator::Shr:
        return ">>";
    case BinaryOperator::Less:
        return "<";
    case BinaryOperator::Greater:
        return ">";
    case BinaryOperator::LessEqual:
        return "<=";
    case BinaryOperator::GreaterEqual:
        return ">=";
    case BinaryOperator::Equal:
        return "==";
    case BinaryOperator::NotEqual:
        return "!=";
    case BinaryOperator::BitwiseAnd:
        return "&";
    case BinaryOperator::BitwiseXor:
        return "^";
    case BinaryOperator::BitwiseOr:
        return "|";
    case BinaryOperator::LogicalAnd:
        return "&&";
    case BinaryOperator::LogicalOr:
        return "||";
    case BinaryOperator::Assign:
        return "=";
    case BinaryOperator::MulAssign:
        return "*=";
    case BinaryOperator::DivAssign:
        return "/=";
    case BinaryOperator::RemAssign:
        return "%=";
    case BinaryOperator::AddAssign:
        return "+=";
    case BinaryOperator::SubAssign:
        return "-=";
    case BinaryOperator::ShlAssign:
        return "<<=";
    case BinaryOperator::ShrAssign:
        return ">>=";
    case BinaryOperator::AndAssign:
        return "&=";
    case BinaryOperator::XorAssign:
        return "^=";
    case BinaryOperator::OrAssign:
        return "|=";
    case BinaryOperator::Comma:
        return ",";
    }
    NITWIT_UNREACHABLE("unknown binary operator");
}

std::string_view castKindName(CastKind kind) {
    switch (kind) {
    case CastKind::LValueToRValue:
        return "LValueToRValue";
    case CastKind::ArrayToPointerDecay:
        return "ArrayToPointerDecay";
    case CastKind::FunctionToPointerDecay:
        return "FunctionToPointerDecay";
    case CastKind::NoOp:
        return "NoOp";
    case CastKind::IntegralCast:
        return "IntegralCast";
    case CastKind::IntegralToBoolean:
        return "IntegralToBoolean";
    case CastKind::IntegralToFloating:
        return "IntegralToFloating";
    case CastKind::IntegralToPointer:
        return "IntegralToPointer";
    case CastKind::PointerToIntegral:
        return "PointerToIntegral";
    case CastKind::PointerToBoolean:
        return "PointerToBoolean";
    case CastKind::NullToPointer:
        return "NullToPointer";
    case CastKind::BitCast:
        return "BitCast";
    case CastKind::FloatingCast:
        return "FloatingCast";
    case CastKind::FloatingToIntegral:
        return "FloatingToIntegral";
    case CastKind::FloatingToBoolean:
        return "FloatingToBoolean";
    case CastKind::ToVoid:
        return "ToVoid";
    case CastKind::ToUnion:
        return "ToUnion";
    }
    NITWIT_UNREACHABLE("unknown cast kind");
}

std::string_view storageName(StorageClass storage) {
    switch (storage) {
    case StorageClass::None:
        return "";
    case StorageClass::Extern:
        return "extern";
    case StorageClass::Static:
        return "static";
    case StorageClass::Auto:
        return "auto";
    case StorageClass::Register:
        return "register";
    }
    NITWIT_UNREACHABLE("unknown storage class");
}

std::string_view declKindName(DeclKind kind) {
    switch (kind) {
    case DeclKind::Var:
        return "VarDecl";
    case DeclKind::Function:
        return "FunctionDecl";
    case DeclKind::Field:
        return "FieldDecl";
    case DeclKind::EnumConstant:
        return "EnumConstantDecl";
    case DeclKind::Typedef:
        return "TypedefDecl";
    case DeclKind::Record:
        return "RecordDecl";
    case DeclKind::Enum:
        return "EnumDecl";
    case DeclKind::StaticAssert:
        return "StaticAssertDecl";
    }
    NITWIT_UNREACHABLE("unknown declaration kind");
}

std::string_view encodingPrefix(lex::CharEncoding encoding) {
    switch (encoding) {
    case lex::CharEncoding::Plain:
        return "";
    case lex::CharEncoding::Wide:
        return "L";
    case lex::CharEncoding::Utf8:
        return "u8";
    case lex::CharEncoding::Utf16:
        return "u";
    case lex::CharEncoding::Utf32:
        return "U";
    }
    NITWIT_UNREACHABLE("unknown character encoding");
}

// Appends `part` to `text`, separated by a space; empty parts are skipped.
void append(std::string& text, std::string_view part) {
    if (part.empty()) {
        return;
    }
    if (!text.empty()) {
        text += ' ';
    }
    text += part;
}

std::string qualifierText(Qualifiers qualifiers) {
    std::string text;
    if (qualifiers.isConst) {
        append(text, "const");
    }
    if (qualifiers.isVolatile) {
        append(text, "volatile");
    }
    if (qualifiers.isRestrict) {
        append(text, "restrict");
    }
    if (qualifiers.isAtomic) {
        append(text, "_Atomic");
    }
    return text;
}

std::string int128Text(Int128 value) {
    if (value == 0) {
        return "0";
    }
    UInt128 magnitude = value < 0 ? -static_cast<UInt128>(value) : static_cast<UInt128>(value);
    std::string digits;
    while (magnitude != 0) {
        digits += static_cast<char>('0' + static_cast<int>(magnitude % 10));
        magnitude /= 10;
    }
    if (value < 0) {
        digits += '-';
    }
    std::ranges::reverse(digits);
    return digits;
}

std::string integerText(const lex::IntegerLiteral& literal) {
    std::string text = std::to_string(literal.value);
    if (literal.hasUnsignedSuffix) {
        text += 'u';
    }
    text.append(literal.longCount, 'l');
    return text;
}

std::string floatingText(const lex::FloatingLiteral& literal) {
    switch (literal.suffix) {
    case lex::FloatingSuffix::None:
        return std::format("{}", literal.doubleValue);
    case lex::FloatingSuffix::F:
        return std::format("{}f", literal.floatValue);
    case lex::FloatingSuffix::L:
        return std::format("{}L", literal.longDoubleValue);
    case lex::FloatingSuffix::F128:
        return std::format("{}f128", literal.longDoubleValue);
    case lex::FloatingSuffix::F32:
        return std::format("{}f32", literal.floatValue);
    case lex::FloatingSuffix::F64:
        return std::format("{}f64", literal.doubleValue);
    case lex::FloatingSuffix::F32x:
        return std::format("{}f32x", literal.doubleValue);
    case lex::FloatingSuffix::F64x:
        return std::format("{}f64x", literal.longDoubleValue);
    }
    NITWIT_UNREACHABLE("unknown floating suffix");
}

std::string stringText(const StringLiteralExpr* literal) {
    std::string text(encodingPrefix(literal->encoding()));
    text += '"';
    for (const std::uint32_t unit : literal->codeUnits()) {
        if (unit == '"' || unit == '\\') {
            text += '\\';
            text += static_cast<char>(unit);
        } else if (unit == '\n') {
            text += "\\n";
        } else if (unit == '\t') {
            text += "\\t";
        } else if (unit >= 0x20 && unit < 0x7F) {
            text += static_cast<char>(unit);
        } else {
            text += std::format("\\x{:02x}", unit);
        }
    }
    text += '"';
    return text;
}

std::string exprText(const Expr* expr);
std::string typeText(QualType type, std::string declarator);

std::string listText(std::span<Expr* const> exprs) {
    std::string text;
    for (const Expr* expr : exprs) {
        if (!text.empty()) {
            text += ", ";
        }
        text += exprText(expr);
    }
    return text;
}

std::string designatorText(std::span<const Designator> designators) {
    std::string text;
    for (const Designator& designator : designators) {
        switch (designator.kind) {
        case Designator::Kind::Field:
            text += '.';
            text += designator.fieldName.text();
            break;
        case Designator::Kind::Index:
            text += std::format("[{}]", exprText(designator.first));
            break;
        case Designator::Kind::Range:
            text += std::format("[{} ... {}]", exprText(designator.first), exprText(designator.last));
            break;
        }
    }
    return text;
}

std::string componentText(std::span<const OffsetOfExpr::Component> components) {
    std::string text;
    for (const OffsetOfExpr::Component& component : components) {
        if (!component.isField) {
            text += std::format("[{}]", exprText(component.index));
            continue;
        }
        if (!text.empty()) {
            text += '.';
        }
        text += component.fieldName.text();
    }
    return text;
}

std::string_view sizeOfName(SizeOfAlignOfExpr::Operator op) {
    switch (op) {
    case SizeOfAlignOfExpr::Operator::SizeOf:
        return "sizeof";
    case SizeOfAlignOfExpr::Operator::AlignOf:
        return "_Alignof";
    case SizeOfAlignOfExpr::Operator::PreferredAlignOf:
        return "__alignof__";
    }
    NITWIT_UNREACHABLE("unknown sizeof operator");
}

// The C spelling of an expression, for array sizes, designators and other expressions inside one dump line.
std::string exprText(const Expr* expr) {
    switch (expr->kind()) {
    case ExprKind::IntegerLiteral:
        return integerText(cast<IntegerLiteralExpr>(expr)->value());
    case ExprKind::FloatingLiteral:
        return floatingText(cast<FloatingLiteralExpr>(expr)->value());
    case ExprKind::CharacterLiteral:
        return std::to_string(cast<CharacterLiteralExpr>(expr)->value().value);
    case ExprKind::StringLiteral:
        return stringText(cast<StringLiteralExpr>(expr));
    case ExprKind::DeclRef:
        return std::string(cast<DeclRefExpr>(expr)->name().text());
    case ExprKind::Paren:
        return std::format("({})", exprText(cast<ParenExpr>(expr)->inner()));
    case ExprKind::Unary: {
        const auto* unary = cast<UnaryExpr>(expr);
        const std::string operand = exprText(unary->operand());
        if (isPostfix(unary->op())) {
            return operand + std::string(unaryOperatorName(unary->op()));
        }
        const std::string_view name = unaryOperatorName(unary->op());
        return std::string(name) + (name.starts_with("__") ? " " : "") + operand;
    }
    case ExprKind::Binary: {
        const auto* binary = cast<BinaryExpr>(expr);
        if (binary->op() == BinaryOperator::Comma) {
            return std::format("{}, {}", exprText(binary->lhs()), exprText(binary->rhs()));
        }
        return std::format("{} {} {}", exprText(binary->lhs()), binaryOperatorName(binary->op()),
                           exprText(binary->rhs()));
    }
    case ExprKind::Conditional: {
        const auto* conditional = cast<ConditionalExpr>(expr);
        if (conditional->trueExpr() == nullptr) {
            return std::format("{} ?: {}", exprText(conditional->condition()), exprText(conditional->falseExpr()));
        }
        return std::format("{} ? {} : {}", exprText(conditional->condition()), exprText(conditional->trueExpr()),
                           exprText(conditional->falseExpr()));
    }
    case ExprKind::Call: {
        const auto* call = cast<CallExpr>(expr);
        return std::format("{}({})", exprText(call->callee()), listText(call->arguments()));
    }
    case ExprKind::Member: {
        const auto* member = cast<MemberExpr>(expr);
        return std::format("{}{}{}", exprText(member->base()), member->isArrow() ? "->" : ".", member->member().text());
    }
    case ExprKind::ArraySubscript: {
        const auto* subscript = cast<ArraySubscriptExpr>(expr);
        return std::format("{}[{}]", exprText(subscript->base()), exprText(subscript->index()));
    }
    case ExprKind::Cast: {
        const auto* castExpr = cast<CastExpr>(expr);
        return std::format("({}){}", typeToString(castExpr->writtenType()), exprText(castExpr->operand()));
    }
    case ExprKind::ImplicitCast:
        return exprText(cast<ImplicitCastExpr>(expr)->operand());
    case ExprKind::CompoundLiteral: {
        const auto* literal = cast<CompoundLiteralExpr>(expr);
        return std::format("({}){}", typeToString(literal->writtenType()), exprText(literal->initializer()));
    }
    case ExprKind::InitList:
        return std::format("{{{}}}", listText(cast<InitListExpr>(expr)->elements()));
    case ExprKind::DesignatedInit: {
        const auto* designated = cast<DesignatedInitExpr>(expr);
        return std::format("{} = {}", designatorText(designated->designators()), exprText(designated->initializer()));
    }
    case ExprKind::ImplicitZero:
        return "0";
    case ExprKind::SizeOfAlignOf: {
        const auto* sizeOf = cast<SizeOfAlignOfExpr>(expr);
        if (sizeOf->operand() != nullptr) {
            return std::format("{} {}", sizeOfName(sizeOf->op()), exprText(sizeOf->operand()));
        }
        return std::format("{}({})", sizeOfName(sizeOf->op()), typeToString(sizeOf->typeOperand()));
    }
    case ExprKind::OffsetOf: {
        const auto* offsetOf = cast<OffsetOfExpr>(expr);
        return std::format("__builtin_offsetof({}, {})", typeToString(offsetOf->recordType()),
                           componentText(offsetOf->components()));
    }
    case ExprKind::VaArg: {
        const auto* vaArg = cast<VaArgExpr>(expr);
        return std::format("__builtin_va_arg({}, {})", exprText(vaArg->list()), typeToString(vaArg->argumentType()));
    }
    case ExprKind::StatementExpr:
        return "({...})";
    case ExprKind::GenericSelection:
        return std::format("_Generic({}, ...)", exprText(cast<GenericSelectionExpr>(expr)->controlling()));
    case ExprKind::ChooseExpr: {
        const auto* choose = cast<ChooseExpr>(expr);
        return std::format("__builtin_choose_expr({}, {}, {})", exprText(choose->condition()),
                           exprText(choose->first()), exprText(choose->second()));
    }
    case ExprKind::TypesCompatible: {
        const auto* compatible = cast<TypesCompatibleExpr>(expr);
        return std::format("__builtin_types_compatible_p({}, {})", typeToString(compatible->first()),
                           typeToString(compatible->second()));
    }
    case ExprKind::Result:
        return "\\result";
    }
    NITWIT_UNREACHABLE("unknown expression kind");
}

std::string arraySizeText(const ArrayType* array) {
    switch (array->sizeKind()) {
    case ArrayType::SizeKind::Constant:
        return std::to_string(array->size());
    case ArrayType::SizeKind::Incomplete:
        return "";
    case ArrayType::SizeKind::Unresolved:
    case ArrayType::SizeKind::Variable:
        return array->sizeExpression() != nullptr ? exprText(array->sizeExpression()) : "*";
    }
    NITWIT_UNREACHABLE("unknown array size kind");
}

std::string tagName(std::string_view keyword, Symbol name) {
    return std::format("{} {}", keyword, name.empty() ? "<anonymous>" : name.text());
}

// The name of a type that is not built from a declarator: builtins, tags, typedef names and the like.
std::string baseTypeText(const Type* type) {
    switch (type->kind()) {
    case TypeKind::Builtin:
        return std::string(builtinName(cast<BuiltinType>(type)->builtinKind()));
    case TypeKind::Record: {
        const RecordDecl* record = cast<RecordType>(type)->decl();
        return tagName(record->isUnion() ? "union" : "struct", record->name());
    }
    case TypeKind::Enum:
        return tagName("enum", cast<EnumType>(type)->decl()->name());
    case TypeKind::Typedef:
        return std::string(cast<TypedefType>(type)->decl()->name().text());
    case TypeKind::Complex:
        return "_Complex " + typeText(cast<ComplexType>(type)->element(), "");
    case TypeKind::Typeof: {
        const auto* typeOf = cast<TypeofType>(type);
        return typeOf->isAutoType() ? "__auto_type" : std::format("typeof({})", exprText(typeOf->expression()));
    }
    case TypeKind::Pointer:
    case TypeKind::Array:
    case TypeKind::Function:
        break;
    }
    NITWIT_UNREACHABLE("not a base type");
}

// The spelling of `type` around the declarator text built so far, from the outermost type inwards, as in C:
// "int (*)[3]" is a pointer to an array, "int *[3]" an array of pointers.
std::string typeText(QualType type, std::string declarator) {
    const Qualifiers qualifiers = type.qualifiers();
    switch (type->kind()) {
    case TypeKind::Pointer: {
        const QualType pointee = cast<PointerType>(type.type())->pointee();
        std::string text = "*" + qualifierText(qualifiers);
        if (!declarator.empty()) {
            if (!qualifiers.empty()) {
                text += ' ';
            }
            text += declarator;
        }
        if (isa<ArrayType>(pointee.type()) || isa<FunctionType>(pointee.type())) {
            text = "(" + text + ")";
        }
        return typeText(pointee, std::move(text));
    }
    case TypeKind::Array: {
        const auto* array = cast<ArrayType>(type.type());
        return typeText(array->element().withQualifiers(qualifiers),
                        std::format("{}[{}]", declarator, arraySizeText(array)));
    }
    case TypeKind::Function: {
        const auto* function = cast<FunctionType>(type.type());
        std::string parameters;
        for (const QualType parameter : function->parameters()) {
            if (!parameters.empty()) {
                parameters += ", ";
            }
            parameters += typeText(parameter, "");
        }
        if (function->isVariadic()) {
            parameters += parameters.empty() ? "..." : ", ...";
        } else if (parameters.empty() && function->hasPrototype()) {
            parameters = "void";
        }
        return typeText(function->returnType(), std::format("{}({})", declarator, parameters));
    }
    default: {
        std::string text = qualifierText(qualifiers);
        append(text, baseTypeText(type.type()));
        append(text, declarator);
        return text;
    }
    }
}

constexpr std::size_t maxIndentedDepth = 64;

class Dumper {
public:
    explicit Dumper(const SourceManager& sources) : sources_(sources) {}

    [[nodiscard]] std::string take() { return std::move(out_); }

    void decl(const Decl* decl);
    void stmt(const Stmt* stmt);
    void expr(const Expr* expr);

private:
    // Indents the lines written during its lifetime by one more level.
    class Nested {
    public:
        explicit Nested(Dumper& dumper) : dumper_(dumper) { ++dumper_.depth_; }
        ~Nested() { --dumper_.depth_; }
        Nested(const Nested&) = delete;
        Nested& operator=(const Nested&) = delete;

    private:
        Dumper& dumper_;
    };

    void indent();
    void line(std::string_view text, SourceLocation location);
    void nullLine();
    [[nodiscard]] std::string position(SourceLocation location) const;
    [[nodiscard]] std::string reference(SourceLocation location) const;
    void attributes(const Decl* decl);
    void varDecl(const VarDecl* var);
    void functionDecl(const FunctionDecl* function);
    void fieldDecl(const FieldDecl* field);
    void recordDecl(const RecordDecl* record);
    void enumDecl(const EnumDecl* enumDecl);
    void optionalStmt(const Stmt* stmt);
    void optionalExpr(const Expr* expr);
    // The kind-specific text of an expression's line and its children, which `expr` then writes.
    [[nodiscard]] std::string exprLine(const Expr* expr);
    void exprChildren(const Expr* expr);

    const SourceManager& sources_;
    std::string out_;
    std::size_t depth_ = 0;
};

// Trees can be deeper than any nesting limit (long operator chains are left-deep), so the indentation stops growing
// and the dump stays linear in the size of the tree.
void Dumper::indent() {
    if (depth_ <= maxIndentedDepth) {
        out_.append(2 * depth_, ' ');
        return;
    }
    out_.append(2 * maxIndentedDepth, ' ');
    out_ += std::format("[{}] ", depth_);
}

void Dumper::line(std::string_view text, SourceLocation location) {
    indent();
    out_ += text;
    out_ += ' ';
    out_ += position(location);
    out_ += '\n';
}

void Dumper::nullLine() {
    indent();
    out_ += "(null)\n";
}

std::string Dumper::position(SourceLocation location) const {
    if (!location.isValid()) {
        return "<invalid>";
    }
    const LineColumn position = sources_.lineColumn(location);
    return std::format("<{}:{}>", position.line, position.column);
}

// "1:5", for a reference to another node.
std::string Dumper::reference(SourceLocation location) const {
    if (!location.isValid()) {
        return "invalid";
    }
    const LineColumn position = sources_.lineColumn(location);
    return std::format("{}:{}", position.line, position.column);
}

void Dumper::attributes(const Decl* decl) {
    for (const Attribute& attribute : decl->attributes()) {
        line(std::format("Attribute {}", attribute.name.text()), attribute.location);
        const Nested nested(*this);
        for (const Expr* argument : attribute.arguments) {
            expr(argument);
        }
    }
}

void Dumper::decl(const Decl* decl) {
    switch (decl->kind()) {
    case DeclKind::Var:
        return varDecl(cast<VarDecl>(decl));
    case DeclKind::Function:
        return functionDecl(cast<FunctionDecl>(decl));
    case DeclKind::Field:
        return fieldDecl(cast<FieldDecl>(decl));
    case DeclKind::EnumConstant: {
        const auto* constant = cast<EnumConstantDecl>(decl);
        std::string text = std::format("EnumConstantDecl {}", constant->name().text());
        if (!constant->type().isNull()) {
            append(text, std::format("'{}' value {}", typeToString(constant->type()), int128Text(constant->value())));
        }
        line(text, constant->location());
        const Nested nested(*this);
        attributes(constant);
        if (constant->initializer() != nullptr) {
            expr(constant->initializer());
        }
        return;
    }
    case DeclKind::Typedef: {
        const auto* typedefDecl = cast<TypedefDecl>(decl);
        line(std::format("TypedefDecl {} '{}'", typedefDecl->name().text(), typeToString(typedefDecl->underlying())),
             typedefDecl->location());
        const Nested nested(*this);
        attributes(typedefDecl);
        return;
    }
    case DeclKind::Record:
        return recordDecl(cast<RecordDecl>(decl));
    case DeclKind::Enum:
        return enumDecl(cast<EnumDecl>(decl));
    case DeclKind::StaticAssert: {
        const auto* staticAssert = cast<StaticAssertDecl>(decl);
        line("StaticAssertDecl", staticAssert->location());
        const Nested nested(*this);
        expr(staticAssert->condition());
        if (staticAssert->message() != nullptr) {
            expr(staticAssert->message());
        }
        return;
    }
    }
    NITWIT_UNREACHABLE("unknown declaration kind");
}

void Dumper::varDecl(const VarDecl* var) {
    std::string text = "VarDecl";
    append(text, var->name().text());
    append(text, std::format("'{}'", typeToString(var->type())));
    append(text, storageName(var->storage()));
    if (var->isThreadLocal()) {
        append(text, "thread_local");
    }
    if (var->isParameter()) {
        append(text, "parameter");
    }
    if (var->previous() != nullptr) {
        append(text, "prev " + reference(var->previous()->location()));
    }
    if (var->requestedAlignment() != 0) {
        append(text, std::format("aligned {}", var->requestedAlignment()));
    }
    line(text, var->location());
    const Nested nested(*this);
    attributes(var);
    if (var->initializer() != nullptr) {
        expr(var->initializer());
    }
}

void Dumper::functionDecl(const FunctionDecl* function) {
    std::string text = std::format("FunctionDecl {} '{}'", function->name().text(), typeToString(function->type()));
    append(text, storageName(function->storage()));
    if (function->isInline()) {
        append(text, "inline");
    }
    if (function->isNoreturn()) {
        append(text, "noreturn");
    }
    if (function->isImplicit()) {
        append(text, "implicit");
    }
    if (function->isWeak()) {
        append(text, "weak");
    }
    if (function->previous() != nullptr) {
        append(text, "prev " + reference(function->previous()->location()));
    }
    line(text, function->location());
    const Nested nested(*this);
    attributes(function);
    for (const VarDecl* parameter : function->parameters()) {
        varDecl(parameter);
    }
    if (function->body() != nullptr) {
        stmt(function->body());
    }
}

void Dumper::fieldDecl(const FieldDecl* field) {
    std::string text = "FieldDecl";
    append(text, field->name().text());
    append(text, std::format("'{}'", typeToString(field->type())));
    if (field->isBitField() && !field->bitWidthExpression()->type().isNull()) {
        append(text, std::format("width {}", field->bitWidth()));
    }
    if (field->isPacked()) {
        append(text, "packed");
    }
    if (field->requestedAlignment() != 0) {
        append(text, std::format("aligned {}", field->requestedAlignment()));
    }
    if (field->parent() != nullptr && field->parent()->layout() != nullptr) {
        const RecordLayout* layout = field->parent()->layout();
        append(text, std::format("offset {}", layout->fieldOffsets[field->index()]));
        if (field->isBitField()) {
            append(text, std::format("bit {}", layout->bitOffsets[field->index()]));
        }
    }
    line(text, field->location());
    const Nested nested(*this);
    attributes(field);
    if (field->isBitField()) {
        expr(field->bitWidthExpression());
    }
}

void Dumper::recordDecl(const RecordDecl* record) {
    std::string text = "RecordDecl " + tagName(record->isUnion() ? "union" : "struct", record->name());
    if (!record->isComplete()) {
        append(text, "incomplete");
    }
    if (record->isPacked()) {
        append(text, "packed");
    }
    if (record->requestedAlignment() != 0) {
        append(text, std::format("aligned {}", record->requestedAlignment()));
    }
    if (record->maxFieldAlignment() != 0) {
        append(text, std::format("max_field_align {}", record->maxFieldAlignment()));
    }
    if (record->layout() != nullptr) {
        append(text, std::format("size {} align {}", record->layout()->size, record->layout()->alignment));
    }
    line(text, record->location());
    const Nested nested(*this);
    attributes(record);
    for (const FieldDecl* field : record->fields()) {
        fieldDecl(field);
    }
}

void Dumper::enumDecl(const EnumDecl* enumDecl) {
    std::string text = "EnumDecl " + tagName("enum", enumDecl->name());
    if (!enumDecl->isComplete()) {
        append(text, "incomplete");
    }
    if (!enumDecl->integerType().isNull()) {
        append(text, std::format("'{}'", typeToString(enumDecl->integerType())));
    }
    line(text, enumDecl->location());
    const Nested nested(*this);
    attributes(enumDecl);
    for (const EnumConstantDecl* constant : enumDecl->constants()) {
        decl(constant);
    }
}

void Dumper::optionalStmt(const Stmt* stmt) {
    if (stmt != nullptr) {
        this->stmt(stmt);
    } else {
        nullLine();
    }
}

void Dumper::optionalExpr(const Expr* expr) {
    if (expr != nullptr) {
        this->expr(expr);
    } else {
        nullLine();
    }
}

void Dumper::stmt(const Stmt* stmt) {
    switch (stmt->kind()) {
    case StmtKind::Compound: {
        line("CompoundStmt", stmt->location());
        const Nested nested(*this);
        for (const Stmt* child : cast<CompoundStmt>(stmt)->body()) {
            this->stmt(child);
        }
        return;
    }
    case StmtKind::Expression: {
        line("ExpressionStmt", stmt->location());
        const Nested nested(*this);
        expr(cast<ExpressionStmt>(stmt)->expr());
        return;
    }
    case StmtKind::Declaration: {
        line("DeclarationStmt", stmt->location());
        const Nested nested(*this);
        for (const Decl* child : cast<DeclarationStmt>(stmt)->decls()) {
            decl(child);
        }
        return;
    }
    case StmtKind::If: {
        const auto* ifStmt = cast<IfStmt>(stmt);
        line("IfStmt", stmt->location());
        const Nested nested(*this);
        expr(ifStmt->condition());
        this->stmt(ifStmt->thenStmt());
        if (ifStmt->elseStmt() != nullptr) {
            this->stmt(ifStmt->elseStmt());
        }
        return;
    }
    case StmtKind::While: {
        const auto* whileStmt = cast<WhileStmt>(stmt);
        line("WhileStmt", stmt->location());
        const Nested nested(*this);
        expr(whileStmt->condition());
        this->stmt(whileStmt->body());
        return;
    }
    case StmtKind::DoWhile: {
        const auto* doStmt = cast<DoWhileStmt>(stmt);
        line("DoWhileStmt", stmt->location());
        const Nested nested(*this);
        this->stmt(doStmt->body());
        expr(doStmt->condition());
        return;
    }
    case StmtKind::For: {
        const auto* forStmt = cast<ForStmt>(stmt);
        line("ForStmt", stmt->location());
        const Nested nested(*this);
        optionalStmt(forStmt->init());
        optionalExpr(forStmt->condition());
        optionalExpr(forStmt->increment());
        this->stmt(forStmt->body());
        return;
    }
    case StmtKind::Switch: {
        const auto* switchStmt = cast<SwitchStmt>(stmt);
        line(std::format("SwitchStmt (cases: {}{})", switchStmt->cases().size(),
                         switchStmt->defaultStmt() != nullptr ? ", default" : ""),
             stmt->location());
        const Nested nested(*this);
        expr(switchStmt->condition());
        this->stmt(switchStmt->body());
        return;
    }
    case StmtKind::Case: {
        const auto* caseStmt = cast<CaseStmt>(stmt);
        std::string text = "CaseStmt";
        if (!caseStmt->value()->type().isNull()) {
            append(text, int128Text(caseStmt->low()));
            if (caseStmt->rangeEnd() != nullptr) {
                append(text, "... " + int128Text(caseStmt->high()));
            }
        }
        line(text, stmt->location());
        const Nested nested(*this);
        expr(caseStmt->value());
        if (caseStmt->rangeEnd() != nullptr) {
            expr(caseStmt->rangeEnd());
        }
        this->stmt(caseStmt->subStmt());
        return;
    }
    case StmtKind::Default: {
        line("DefaultStmt", stmt->location());
        const Nested nested(*this);
        this->stmt(cast<DefaultStmt>(stmt)->subStmt());
        return;
    }
    case StmtKind::Break:
        return line("BreakStmt", stmt->location());
    case StmtKind::Continue:
        return line("ContinueStmt", stmt->location());
    case StmtKind::Return: {
        line("ReturnStmt", stmt->location());
        const Nested nested(*this);
        if (const Expr* value = cast<ReturnStmt>(stmt)->value(); value != nullptr) {
            expr(value);
        }
        return;
    }
    case StmtKind::Goto: {
        const auto* gotoStmt = cast<GotoStmt>(stmt);
        const std::string target = gotoStmt->target() != nullptr
                                       ? std::format("(LabelStmt at {})", reference(gotoStmt->target()->location()))
                                       : "(unresolved)";
        return line(std::format("GotoStmt {} {}", gotoStmt->label().text(), target), stmt->location());
    }
    case StmtKind::Label: {
        const auto* label = cast<LabelStmt>(stmt);
        line(std::format("LabelStmt {}", label->name().text()), stmt->location());
        const Nested nested(*this);
        this->stmt(label->subStmt());
        return;
    }
    case StmtKind::Null:
        return line("NullStmt", stmt->location());
    case StmtKind::Asm: {
        line("AsmStmt", stmt->location());
        const Nested nested(*this);
        expr(cast<AsmStmt>(stmt)->asmTemplate());
        return;
    }
    }
    NITWIT_UNREACHABLE("unknown statement kind");
}

void Dumper::expr(const Expr* expr) {
    std::string text = exprLine(expr);
    if (!expr->type().isNull()) {
        append(text, std::format("'{}'", typeToString(expr->type())));
        if (expr->isLValue()) {
            append(text, "lvalue");
        }
    }
    line(text, expr->location());
    const Nested nested(*this);
    exprChildren(expr);
}

std::string Dumper::exprLine(const Expr* expr) {
    switch (expr->kind()) {
    case ExprKind::IntegerLiteral:
        return "IntegerLiteral " + integerText(cast<IntegerLiteralExpr>(expr)->value());
    case ExprKind::FloatingLiteral:
        return "FloatingLiteral " + floatingText(cast<FloatingLiteralExpr>(expr)->value());
    case ExprKind::CharacterLiteral: {
        const lex::CharacterLiteral& value = cast<CharacterLiteralExpr>(expr)->value();
        std::string text = "CharacterLiteral";
        append(text, encodingPrefix(value.encoding));
        append(text, std::to_string(value.value));
        return text;
    }
    case ExprKind::StringLiteral:
        return "StringLiteral " + stringText(cast<StringLiteralExpr>(expr));
    case ExprKind::DeclRef: {
        const auto* declRef = cast<DeclRefExpr>(expr);
        if (declRef->decl() == nullptr) {
            return std::format("DeclRefExpr {} (unbound)", declRef->name().text());
        }
        return std::format("DeclRefExpr {} ({} at {})", declRef->name().text(), declKindName(declRef->decl()->kind()),
                           reference(declRef->decl()->location()));
    }
    case ExprKind::Paren:
        return "ParenExpr";
    case ExprKind::Unary: {
        const UnaryOperator op = cast<UnaryExpr>(expr)->op();
        return std::format("UnaryExpr {}{}", isPostfix(op) ? "postfix " : "", unaryOperatorName(op));
    }
    case ExprKind::Binary:
        return std::format("BinaryExpr {}", binaryOperatorName(cast<BinaryExpr>(expr)->op()));
    case ExprKind::Conditional:
        return cast<ConditionalExpr>(expr)->trueExpr() == nullptr ? "ConditionalExpr ?:" : "ConditionalExpr";
    case ExprKind::Call:
        return "CallExpr";
    case ExprKind::Member: {
        const auto* member = cast<MemberExpr>(expr);
        return std::format("MemberExpr {}{}", member->isArrow() ? "->" : ".", member->member().text());
    }
    case ExprKind::ArraySubscript:
        return "ArraySubscriptExpr";
    case ExprKind::Cast: {
        const auto* castExpr = cast<CastExpr>(expr);
        std::string text = std::format("CastExpr ({})", typeToString(castExpr->writtenType()));
        if (!castExpr->type().isNull()) {
            append(text, castKindName(castExpr->castKind()));
        }
        return text;
    }
    case ExprKind::ImplicitCast:
        return std::format("ImplicitCastExpr {}", castKindName(cast<ImplicitCastExpr>(expr)->castKind()));
    case ExprKind::CompoundLiteral: {
        const auto* literal = cast<CompoundLiteralExpr>(expr);
        return std::format("CompoundLiteralExpr ({}){}", typeToString(literal->writtenType()),
                           literal->isFileScope() ? " file_scope" : "");
    }
    case ExprKind::InitList: {
        const auto* list = cast<InitListExpr>(expr);
        std::string text = "InitListExpr";
        if (list->isSemanticForm()) {
            append(text, "semantic");
        }
        if (list->unionField() != nullptr) {
            append(text, std::format("union_field {}", list->unionField()->name().text()));
        }
        return text;
    }
    case ExprKind::DesignatedInit:
        return "DesignatedInitExpr " + designatorText(cast<DesignatedInitExpr>(expr)->designators());
    case ExprKind::ImplicitZero:
        return "ImplicitZeroExpr";
    case ExprKind::SizeOfAlignOf: {
        const auto* sizeOf = cast<SizeOfAlignOfExpr>(expr);
        if (sizeOf->operand() != nullptr) {
            return std::format("SizeOfAlignOfExpr {}", sizeOfName(sizeOf->op()));
        }
        return std::format("SizeOfAlignOfExpr {} ({})", sizeOfName(sizeOf->op()), typeToString(sizeOf->typeOperand()));
    }
    case ExprKind::OffsetOf: {
        const auto* offsetOf = cast<OffsetOfExpr>(expr);
        return std::format("OffsetOfExpr ({}) {}", typeToString(offsetOf->recordType()),
                           componentText(offsetOf->components()));
    }
    case ExprKind::VaArg:
        return std::format("VaArgExpr ({})", typeToString(cast<VaArgExpr>(expr)->argumentType()));
    case ExprKind::StatementExpr:
        return "StatementExpr";
    case ExprKind::GenericSelection:
        return "GenericSelectionExpr";
    case ExprKind::ChooseExpr:
        return "ChooseExpr";
    case ExprKind::TypesCompatible: {
        const auto* compatible = cast<TypesCompatibleExpr>(expr);
        return std::format("TypesCompatibleExpr ({}, {})", typeToString(compatible->first()),
                           typeToString(compatible->second()));
    }
    case ExprKind::Result:
        return "ResultExpr";
    }
    NITWIT_UNREACHABLE("unknown expression kind");
}

void Dumper::exprChildren(const Expr* expr) {
    switch (expr->kind()) {
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatingLiteral:
    case ExprKind::CharacterLiteral:
    case ExprKind::StringLiteral:
    case ExprKind::DeclRef:
    case ExprKind::ImplicitZero:
    case ExprKind::TypesCompatible:
    case ExprKind::Result:
        return;
    case ExprKind::Paren:
        return this->expr(cast<ParenExpr>(expr)->inner());
    case ExprKind::Unary:
        return this->expr(cast<UnaryExpr>(expr)->operand());
    case ExprKind::Binary: {
        const auto* binary = cast<BinaryExpr>(expr);
        this->expr(binary->lhs());
        return this->expr(binary->rhs());
    }
    case ExprKind::Conditional: {
        const auto* conditional = cast<ConditionalExpr>(expr);
        this->expr(conditional->condition());
        if (conditional->trueExpr() != nullptr) {
            this->expr(conditional->trueExpr());
        }
        return this->expr(conditional->falseExpr());
    }
    case ExprKind::Call: {
        const auto* call = cast<CallExpr>(expr);
        this->expr(call->callee());
        for (const Expr* argument : call->arguments()) {
            this->expr(argument);
        }
        return;
    }
    case ExprKind::Member:
        return this->expr(cast<MemberExpr>(expr)->base());
    case ExprKind::ArraySubscript: {
        const auto* subscript = cast<ArraySubscriptExpr>(expr);
        this->expr(subscript->base());
        return this->expr(subscript->index());
    }
    case ExprKind::Cast:
        return this->expr(cast<CastExpr>(expr)->operand());
    case ExprKind::ImplicitCast:
        return this->expr(cast<ImplicitCastExpr>(expr)->operand());
    case ExprKind::CompoundLiteral:
        return this->expr(cast<CompoundLiteralExpr>(expr)->initializer());
    case ExprKind::InitList:
        for (const Expr* element : cast<InitListExpr>(expr)->elements()) {
            this->expr(element);
        }
        return;
    case ExprKind::DesignatedInit:
        return this->expr(cast<DesignatedInitExpr>(expr)->initializer());
    case ExprKind::SizeOfAlignOf:
        if (const Expr* operand = cast<SizeOfAlignOfExpr>(expr)->operand(); operand != nullptr) {
            this->expr(operand);
        }
        return;
    case ExprKind::OffsetOf:
        return;
    case ExprKind::VaArg:
        return this->expr(cast<VaArgExpr>(expr)->list());
    case ExprKind::StatementExpr:
        return stmt(cast<StatementExpr>(expr)->body());
    case ExprKind::GenericSelection: {
        const auto* generic = cast<GenericSelectionExpr>(expr);
        this->expr(generic->controlling());
        for (const GenericSelectionExpr::Association& association : generic->associations()) {
            line(association.type.isNull() ? "GenericAssociation default"
                                           : std::format("GenericAssociation ({})", typeToString(association.type)),
                 association.result->location());
            const Nested nested(*this);
            this->expr(association.result);
        }
        return;
    }
    case ExprKind::ChooseExpr: {
        const auto* choose = cast<ChooseExpr>(expr);
        this->expr(choose->condition());
        this->expr(choose->first());
        return this->expr(choose->second());
    }
    }
    NITWIT_UNREACHABLE("unknown expression kind");
}

} // namespace

std::string typeToString(QualType type) {
    if (type.isNull()) {
        return "<null type>";
    }
    return typeText(type, "");
}

std::string dump(const TranslationUnit& unit, const SourceManager& sources) {
    Dumper dumper(sources);
    for (const Decl* decl : unit.decls) {
        dumper.decl(decl);
    }
    return dumper.take();
}

std::string dump(const Decl* decl, const SourceManager& sources) {
    Dumper dumper(sources);
    dumper.decl(decl);
    return dumper.take();
}

std::string dump(const Stmt* stmt, const SourceManager& sources) {
    Dumper dumper(sources);
    dumper.stmt(stmt);
    return dumper.take();
}

std::string dump(const Expr* expr, const SourceManager& sources) {
    Dumper dumper(sources);
    dumper.expr(expr);
    return dumper.take();
}

} // namespace nitwit::ast
