// Declarations (C17 6.7, 6.9): their types, attributes, redeclarations and initializers.

#include <bit>
#include <format>
#include <string>

#include "ast/casting.hpp"
#include "sema/constant.hpp"
#include "sema/sema_impl.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::sema::detail {

namespace {

// GCC's BIGGEST_ALIGNMENT on x86 with SSE, the alignment of `aligned` without an argument.
constexpr std::uint64_t biggestAlignment = 16;
// GCC rejects larger alignments for objects (MAX_OFILE_ALIGNMENT is 2^28 bytes on ELF targets).
constexpr std::uint64_t maxAlignment = std::uint64_t{1} << 28;

// The attributes that change the meaning of a program in ways NITWIT does not model.
constexpr std::string_view unsupportedAttributes[] = {"cleanup", "vector_size", "ifunc"};

} // namespace

const ast::Attribute* Sema::findAttribute(const ast::Decl* decl, std::string_view name) {
    const ast::Attribute* found = nullptr;
    for (const ast::Attribute& attribute : decl->attributes()) {
        if (attribute.name.text() == name) {
            found = &attribute; // the last one wins, as for repeated attributes in GCC
        }
    }
    return found;
}

// Also checks `alias("target")`, which the interpreter resolves: the declaration names the same object or function
// as the target (Linux kernel modules alias init_module and device tables).
void Sema::checkUnsupportedAttributes(const ast::Decl* decl) {
    for (const ast::Attribute& attribute : decl->attributes()) {
        for (const std::string_view name : unsupportedAttributes) {
            if (attribute.name.text() == name) {
                error(attribute.location, std::format("the attribute '{}' is not supported", name));
            }
        }
        if (attribute.name.text() == "alias"
            && (attribute.arguments.size() != 1 || !ast::isa<ast::StringLiteralExpr>(attribute.arguments.front()))) {
            error(attribute.location, "the argument of the attribute 'alias' must be a string");
        }
    }
}

std::uint64_t Sema::alignmentArgument(const ast::Attribute& attribute) {
    if (attribute.arguments.empty()) {
        return biggestAlignment;
    }
    ast::Expr* argument = attribute.arguments.front();
    expr(argument);
    const Int128 value = integerConstant(argument, "requested alignment");
    if (value <= 0 || value > static_cast<Int128>(maxAlignment)
        || !std::has_single_bit(static_cast<std::uint64_t>(value))) {
        error(attribute.location, "requested alignment is not a positive power of 2");
    }
    return static_cast<std::uint64_t>(value);
}

// GCC's `mode` attribute (c_common_type_for_mode): QI, HI, SI, DI and TI select integers of 1, 2, 4, 8 and 16 bytes
// with the signedness of the declared type; word and pointer select the pointer width.
std::optional<ast::QualType> Sema::modeType(const ast::Decl* decl, ast::QualType type) {
    const ast::Attribute* attribute = findAttribute(decl, "mode");
    if (attribute == nullptr) {
        return std::nullopt;
    }
    const auto* name =
        attribute->arguments.size() == 1 ? ast::dyn_cast<ast::DeclRefExpr>(attribute->arguments[0]) : nullptr;
    if (name == nullptr) {
        error(attribute->location, "the attribute 'mode' requires a machine mode");
    }
    std::string_view mode = name->name().text();
    if (mode.size() > 4 && mode.starts_with("__") && mode.ends_with("__")) {
        mode = mode.substr(2, mode.size() - 4);
    }
    std::uint64_t bytes = 0;
    if (mode == "QI" || mode == "byte") {
        bytes = 1;
    } else if (mode == "HI") {
        bytes = 2;
    } else if (mode == "SI") {
        bytes = 4;
    } else if (mode == "DI") {
        bytes = 8;
    } else if (mode == "TI") {
        bytes = 16;
    } else if (mode == "word" || mode == "pointer" || mode == "unwind_word") {
        bytes = context_.target().pointerBits() / 8;
    } else {
        error(attribute->location, std::format("the machine mode '{}' is not supported", mode));
    }
    if (!isIntegerClass(classify(context_, type))) {
        error(attribute->location, "the attribute 'mode' is only supported for integer types");
    }
    if (bytes == 16 && !context_.target().hasInt128()) {
        error(attribute->location, "unable to emulate 'TI'");
    }
    return integerTypeForSize(context_, bytes, integerFormat(context_, type).isSigned)
        .withQualifiers(type.qualifiers());
}

// ---- Types as written ----

void Sema::resolveType(ast::QualType type, SourceLocation location) {
    if (type.isNull() || resolvedTypes_.contains(type.type())) {
        return;
    }
    switch (type->kind()) {
    case ast::TypeKind::Builtin:
        if (ast::cast<ast::BuiltinType>(type.type())->builtinKind() == ast::BuiltinKind::Int128
            || ast::cast<ast::BuiltinType>(type.type())->builtinKind() == ast::BuiltinKind::UnsignedInt128) {
            if (!context_.target().hasInt128()) {
                error(location, "'__int128' is not supported on this target");
            }
        }
        break;
    case ast::TypeKind::Pointer:
        resolveType(ast::cast<ast::PointerType>(type.type())->pointee(), location);
        break;
    case ast::TypeKind::Array:
        resolveArraySize(ast::cast<ast::ArrayType>(type.type()), location);
        break;
    case ast::TypeKind::Function: {
        const auto* function = ast::cast<ast::FunctionType>(type.type());
        resolveType(function->returnType(), location);
        ++prototypeDepth_;
        for (const ast::QualType parameter : function->parameters()) {
            resolveType(parameter, location);
        }
        --prototypeDepth_;
        const TypeClass returnClass = classify(context_, function->returnType());
        if (returnClass == TypeClass::Array || returnClass == TypeClass::Function) {
            error(location, std::format("function cannot return {} type '{}'",
                                        returnClass == TypeClass::Array ? "array" : "function",
                                        typeName(function->returnType())));
        }
        break;
    }
    case ast::TypeKind::Record:
        return; // completed where a complete type is needed; not cached, since that may happen later
    case ast::TypeKind::Enum: {
        ast::EnumDecl* enumDecl = ast::cast<ast::EnumType>(type.type())->decl();
        if (!enumDecl->isComplete()) {
            return;
        }
        completeEnum(enumDecl, location);
        break;
    }
    case ast::TypeKind::Typedef:
        resolveType(ast::cast<ast::TypedefType>(type.type())->decl()->underlying(), location);
        break;
    case ast::TypeKind::Complex: {
        const ast::QualType element = ast::cast<ast::ComplexType>(type.type())->element();
        resolveType(element, location);
        if (!isArithmeticClass(classify(context_, element))) {
            error(location, std::format("invalid complex type '{}'", typeName(type)));
        }
        break;
    }
    case ast::TypeKind::Typeof: {
        const auto* typeOf = ast::cast<ast::TypeofType>(type.type());
        if (typeOf->resolved().isNull()) {
            ast::Expr* operand = typeOf->expression();
            expr(operand);
            if (typeOf->isAutoType()) {
                typeOf->resolve(valueType(operand));
            } else {
                if (const auto* member = ast::dyn_cast<ast::MemberExpr>(ast::ignoreParens(operand));
                    member != nullptr && member->field()->isBitField()) {
                    error(operand->location(), "'typeof' applied to a bit-field");
                }
                typeOf->resolve(operand->type());
            }
        }
        break;
    }
    }
    resolvedTypes_.insert(type.type());
}

// C17 6.7.6.2: the size is an integer constant expression greater than zero (GNU: or zero), and the element type has a
// constant size; otherwise the array is a variable length array, which is only allowed in block and function
// prototype scope.
void Sema::resolveArraySize(const ast::ArrayType* array, SourceLocation location) {
    const ast::QualType element = array->element();
    resolveType(element, location);
    if (classify(context_, element) == TypeClass::Function) {
        error(location, "declaration of an array of functions");
    }
    requireComplete(element, location, "array type has incomplete element type");
    if (array->sizeKind() != ast::ArrayType::SizeKind::Unresolved) {
        return;
    }
    ast::Expr* size = array->sizeExpression();
    expr(size);
    if (!isIntegerClass(classify(context_, size->type()))) {
        error(size->location(), "size of array has non-integer type");
    }
    const std::optional<Int128> value = evaluateIntegerConstant(context_, size);
    if (value && !isVariablySized(element)) {
        if (*value < 0) {
            error(size->location(), "size of array is negative");
        }
        const std::uint64_t elementSize = sizeOf(context_, element);
        const Int128 limit =
            context_.target().dataModel() == DataModel::ILP32 ? Int128{0x7fffffff} : Int128{0x7fffffffffffffff};
        if (elementSize != 0 && *value > limit / static_cast<Int128>(elementSize)) {
            error(size->location(), "size of array is too large");
        }
        array->resolveConstantSize(static_cast<std::uint64_t>(*value));
        return;
    }
    if (!function_ && prototypeDepth_ == 0) {
        error(size->location(), "variably modified type at file scope");
    }
    array->resolveVariableSize();
    array->setSizeExpression(promote(size));
}

// ---- Declarations ----

void Sema::declaration(ast::Decl* decl) {
    switch (decl->kind()) {
    case ast::DeclKind::Var:
        return varDecl(ast::cast<ast::VarDecl>(decl));
    case ast::DeclKind::Function:
        return functionDecl(ast::cast<ast::FunctionDecl>(decl));
    case ast::DeclKind::Typedef:
        return typedefDecl(ast::cast<ast::TypedefDecl>(decl));
    case ast::DeclKind::Record: {
        auto* record = ast::cast<ast::RecordDecl>(decl);
        if (record->isComplete()) {
            completeRecord(record);
        }
        return;
    }
    case ast::DeclKind::Enum: {
        auto* enumDecl = ast::cast<ast::EnumDecl>(decl);
        if (enumDecl->isComplete()) {
            completeEnum(enumDecl, enumDecl->location());
        }
        return;
    }
    case ast::DeclKind::StaticAssert:
        return staticAssert(ast::cast<ast::StaticAssertDecl>(decl));
    case ast::DeclKind::Field:
    case ast::DeclKind::EnumConstant:
        break;
    }
    NITWIT_UNREACHABLE("unexpected declaration in a declaration list");
}

void Sema::typedefDecl(ast::TypedefDecl* typedefDecl) {
    checkUnsupportedAttributes(typedefDecl);
    resolveType(typedefDecl->underlying(), typedefDecl->location());
    if (const std::optional<ast::QualType> type = modeType(typedefDecl, typedefDecl->underlying())) {
        typedefDecl->setUnderlying(*type);
    }
    if (const ast::Attribute* aligned = findAttribute(typedefDecl, "aligned")) {
        typedefDecl->setRequestedAlignment(alignmentArgument(*aligned));
    }
}

void Sema::staticAssert(ast::StaticAssertDecl* staticAssert) {
    ast::Expr* condition = staticAssert->condition();
    expr(condition);
    if (!isIntegerClass(classify(context_, condition->type()))) {
        error(condition->location(), "static assertion expression is not an integer");
    }
    if (integerConstant(condition, "static assertion expression") == 0) {
        std::string message = "static assertion failed";
        if (const auto* text = ast::dyn_cast<ast::StringLiteralExpr>(staticAssert->message())) {
            message += ": \"";
            for (const std::uint32_t unit : text->codeUnits()) {
                message += static_cast<char>(unit);
            }
            message += '"';
        }
        error(staticAssert->location(), message);
    }
}

void Sema::varDecl(ast::VarDecl* var) {
    checkUnsupportedAttributes(var);
    const bool isAutomatic = !var->hasStaticStorage();
    resolveType(var->type(), var->location());
    if (const std::optional<ast::QualType> type = modeType(var, var->type())) {
        var->setType(*type);
    }
    if (const ast::Attribute* aligned = findAttribute(var, "aligned")) {
        var->setRequestedAlignment(alignmentArgument(*aligned));
    }
    const TypeClass typeClass = classify(context_, var->type());
    // GCC accepts `extern void x;`, a symbol only used for its address.
    if (typeClass == TypeClass::Void
        && (var->storage() != ast::StorageClass::Extern || var->initializer() != nullptr)) {
        error(var->location(), std::format("variable '{}' declared void", var->name().text()));
    }
    if (typeClass == TypeClass::Function) {
        // The parser declares a variable for `typeof(*fp) g;`, whose type it cannot see.
        error(var->location(),
              std::format("a function declared through 'typeof' ('{}') is not supported", var->name().text()));
    }
    if (const ast::ArrayType* array = arrayOf(var->type());
        array != nullptr && array->sizeKind() == ast::ArrayType::SizeKind::Variable && !isAutomatic) {
        error(var->location(), std::format("storage size of '{}' isn't constant", var->name().text()));
    }
    mergeVariable(var);
    if (isAutomatic) {
        // Before the initializer, which may declare locals of its own in statement expressions.
        NITWIT_ASSERT(function_.has_value(), "automatic variable outside of a function");
        var->setLocalIndex(static_cast<std::uint32_t>(function_->locals.size()));
        function_->locals.push_back(var);
    }

    if (ast::Expr* init = var->initializer()) {
        if (!var->isFileScope() && var->storage() == ast::StorageClass::Extern) {
            error(var->location(), std::format("'{}' has both 'extern' and initializer", var->name().text()));
        }
        if (var->isFileScope() && var->storage() == ast::StorageClass::Extern) {
            warning(var->location(), std::format("'{}' initialized and declared 'extern'", var->name().text()));
        }
        for (const ast::VarDecl* previous = var->previous(); previous != nullptr; previous = previous->previous()) {
            if (previous->initializer() != nullptr) {
                error(var->location(), std::format("redefinition of '{}'", var->name().text()));
            }
        }
        if (const ast::ArrayType* array = arrayOf(var->type());
            array != nullptr && array->sizeKind() == ast::ArrayType::SizeKind::Variable) {
            error(var->location(), "variable-sized object may not be initialized");
        }
        ast::QualType type = var->type();
        var->setInitializer(initializer(type, init));
        var->setType(type);
    }

    if (var->isFileScope()) {
        fileScopeVariables_.push_back(var);
        return;
    }
    if (var->storage() != ast::StorageClass::Extern) {
        requireComplete(var->type(), var->location(),
                        std::format("storage size of '{}' isn't known", var->name().text()));
    }
}

// C17 6.2.7p4: a declaration of an object declared before gets the composite type of both.
void Sema::mergeVariable(ast::VarDecl* var) {
    const ast::VarDecl* previous = var->previous();
    if (previous == nullptr) {
        return;
    }
    if (!compatible(context_, previous->type(), var->type())) {
        error(var->location(), std::format("conflicting types for '{}'; have '{}', previously '{}'", var->name().text(),
                                           typeName(var->type()), typeName(previous->type())));
    }
    if (context_.canonical(previous->type()) != context_.canonical(var->type())) {
        var->setType(composite(context_, previous->type(), var->type()));
    }
}

// C17 6.7.6.3p7-8: a parameter of array type is a pointer to the element, one of function type a pointer to the
// function. The parser adjusts the types it can see; types written with typeof are adjusted here.
ast::QualType Sema::adjustedParameterType(ast::QualType type) {
    switch (classify(context_, type)) {
    case TypeClass::Array: {
        const ast::QualType array = desugar(type);
        return context_.pointerTo(
            ast::cast<ast::ArrayType>(array.type())->element().withQualifiers(array.qualifiers()));
    }
    case TypeClass::Function:
        return context_.pointerTo(type);
    default:
        return type;
    }
}

void Sema::parameter(ast::VarDecl* parameter) {
    checkUnsupportedAttributes(parameter);
    ++prototypeDepth_;
    resolveType(parameter->type(), parameter->location());
    --prototypeDepth_;
    if (const std::optional<ast::QualType> type = modeType(parameter, parameter->type())) {
        parameter->setType(*type);
    }
    parameter->setType(adjustedParameterType(parameter->type()));
}

void Sema::adjustParameterTypes(ast::FunctionDecl* function) {
    const ast::FunctionType* type = functionOf(function->type());
    if (type == nullptr || !type->hasPrototype()) {
        return;
    }
    std::vector<ast::QualType> parameters(type->parameters().begin(), type->parameters().end());
    bool isAdjusted = false;
    for (ast::QualType& parameter : parameters) {
        const ast::QualType adjusted = adjustedParameterType(parameter);
        isAdjusted = isAdjusted || adjusted != parameter;
        parameter = adjusted;
    }
    if (isAdjusted) {
        function->setType(context_.functionType(type->returnType(), parameters, type->isVariadic(), true));
    }
}

void Sema::functionDecl(ast::FunctionDecl* function) {
    checkUnsupportedAttributes(function);
    ++prototypeDepth_;
    resolveType(function->type(), function->location());
    --prototypeDepth_;
    adjustParameterTypes(function);
    for (ast::VarDecl* parameter : function->parameters()) {
        this->parameter(parameter);
    }
    if (function->isImplicit() && function->previous() != nullptr) {
        // A call of a function declared in a block that has ended refers to that function.
        function->setType(function->previous()->type());
    } else if (function->isImplicit()) {
        applyBuiltinSignature(function);
    }
    if (findAttribute(function, "noreturn") != nullptr) {
        function->setNoreturn();
    }
    if (findAttribute(function, "weak") != nullptr) {
        for (ast::FunctionDecl* redeclaration = function; redeclaration != nullptr;
             redeclaration = redeclaration->previous()) {
            redeclaration->setWeak();
        }
    }
    mergeFunction(function);
    if (function->body() != nullptr) {
        functionBody(function);
    }
}

// An implicitly declared function that GCC knows as a builtin gets its builtin signature (GCC: "incompatible implicit
// declaration of built-in function").
void Sema::applyBuiltinSignature(ast::FunctionDecl* function) {
    const std::optional<BuiltinInfo> builtin = lookupBuiltin(context_, function->name().text());
    if (!builtin) {
        if (function->name().text().starts_with("__builtin_")) {
            warning(function->location(),
                    std::format("unknown builtin function '{}' is treated as 'int ()'", function->name().text()));
        }
        return;
    }
    if (builtin->isNoreturn) {
        function->setNoreturn();
    }
    if (builtin->signature.isNull()) {
        return; // a type-generic builtin, typed at each call
    }
    if (builtin->isLibraryFunction && !compatible(context_, function->type(), builtin->signature)) {
        warning(function->location(),
                std::format("incompatible implicit declaration of built-in function '{}'", function->name().text()));
    }
    function->setType(builtin->signature);
}

// C17 6.2.7p4 for functions. A declaration that conflicts with an implicit declaration replaces its type with a
// warning; GCC rejects some of these, but old sv-benchmarks rely on the others.
void Sema::mergeFunction(ast::FunctionDecl* function) {
    const ast::FunctionDecl* previous = function->previous();
    if (previous == nullptr) {
        return;
    }
    if (!compatible(context_, previous->type(), function->type())) {
        const std::string message =
            std::format("conflicting types for '{}'; have '{}', previously '{}'", function->name().text(),
                        typeName(function->type()), typeName(previous->type()));
        bool previousIsImplicit = false;
        for (const ast::FunctionDecl* declaration = previous; declaration != nullptr;
             declaration = declaration->previous()) {
            previousIsImplicit = previousIsImplicit || declaration->isImplicit();
        }
        if (!previousIsImplicit) {
            error(function->location(), message);
        }
        warning(function->location(), message);
        return;
    }
    if (previous->isNoreturn()) {
        function->setNoreturn();
    }
    if (context_.canonical(previous->type()) != context_.canonical(function->type())) {
        function->setType(composite(context_, previous->type(), function->type()));
    }
}

} // namespace nitwit::sema::detail
