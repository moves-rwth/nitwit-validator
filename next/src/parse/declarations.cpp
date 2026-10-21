// Declarations: specifiers, declarators, struct/union/enum, attributes, function definitions (C17 6.7, 6.9).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#include "ast/casting.hpp"
#include "lex/lexer.hpp"
#include "parse/parser_impl.hpp"
#include "support/assert.hpp"

namespace nitwit::parse::detail {

namespace {

using ast::BuiltinKind;

// The counter of a type specifier keyword, or null if `kind` is not one. _Float32 and friends count as the standard
// type they map to; _Float64x is long double on x86, and _Float128 is approximated by it.
unsigned* keywordCounter(TypeSpecifiers& types, TokenKind kind) {
    switch (kind) {
    case TokenKind::KwVoid:
        return &types.voidCount;
    case TokenKind::KwBool:
        return &types.boolCount;
    case TokenKind::KwChar:
        return &types.charCount;
    case TokenKind::KwInt:
        return &types.intCount;
    case TokenKind::KwFloat:
    case TokenKind::KwFloat32:
        return &types.floatCount;
    case TokenKind::KwDouble:
    case TokenKind::KwFloat64:
    case TokenKind::KwFloat32x:
        return &types.doubleCount;
    case TokenKind::KwFloat64x:
    case TokenKind::KwFloat128:
        ++types.longCount;
        return &types.doubleCount;
    case TokenKind::KwInt128:
        return &types.int128Count;
    case TokenKind::KwShort:
        return &types.shortCount;
    case TokenKind::KwLong:
        return &types.longCount;
    case TokenKind::KwSigned:
        return &types.signedCount;
    case TokenKind::KwUnsigned:
        return &types.unsignedCount;
    case TokenKind::KwComplex:
        return &types.complexCount;
    default:
        return nullptr;
    }
}

// The builtin type of a combination of keywords, or nothing if the combination is invalid.
std::optional<BuiltinKind> builtinKind(const TypeSpecifiers& s) {
    const unsigned bases =
        s.voidCount + s.boolCount + s.charCount + s.intCount + s.floatCount + s.doubleCount + s.int128Count;
    const bool isUnsigned = s.unsignedCount > 0;
    const bool hasSign = s.signedCount + s.unsignedCount > 0;
    if (bases > 1 || s.signedCount + s.unsignedCount > 1 || s.shortCount > 1 || s.longCount > 2
        || (s.shortCount > 0 && s.longCount > 0)) {
        return std::nullopt;
    }
    const bool hasModifiers = hasSign || s.shortCount > 0 || s.longCount > 0;
    if (s.voidCount > 0 || s.boolCount > 0) {
        return hasModifiers ? std::nullopt : std::optional(s.voidCount > 0 ? BuiltinKind::Void : BuiltinKind::Bool);
    }
    if (s.floatCount > 0) {
        return hasModifiers ? std::nullopt : std::optional(BuiltinKind::Float);
    }
    if (s.doubleCount > 0) {
        if (hasSign || s.shortCount > 0 || s.longCount > 1) {
            return std::nullopt;
        }
        return s.longCount == 1 ? BuiltinKind::LongDouble : BuiltinKind::Double;
    }
    if (s.charCount > 0) {
        if (s.shortCount > 0 || s.longCount > 0) {
            return std::nullopt;
        }
        return s.signedCount > 0 ? BuiltinKind::SignedChar : isUnsigned ? BuiltinKind::UnsignedChar : BuiltinKind::Char;
    }
    if (s.int128Count > 0) {
        if (s.shortCount > 0 || s.longCount > 0) {
            return std::nullopt;
        }
        return isUnsigned ? BuiltinKind::UnsignedInt128 : BuiltinKind::Int128;
    }
    if (s.intCount == 0 && !hasModifiers) {
        return BuiltinKind::Double; // GNU: `_Complex` alone means `_Complex double`
    }
    if (s.shortCount > 0) {
        return isUnsigned ? BuiltinKind::UnsignedShort : BuiltinKind::Short;
    }
    if (s.longCount == 1) {
        return isUnsigned ? BuiltinKind::UnsignedLong : BuiltinKind::Long;
    }
    if (s.longCount == 2) {
        return isUnsigned ? BuiltinKind::UnsignedLongLong : BuiltinKind::LongLong;
    }
    return isUnsigned ? BuiltinKind::UnsignedInt : BuiltinKind::Int;
}

// The type the type specifiers name: null if there are none, nothing if they do not combine.
std::optional<ast::QualType> specifiedType(ast::AstContext& context, const TypeSpecifiers& types) {
    if (types.namedCount > 0) {
        if (types.namedCount > 1 || types.keywordCount() > 0) {
            return std::nullopt;
        }
        return types.named;
    }
    if (types.keywordCount() == 0) {
        return ast::QualType();
    }
    const std::optional<BuiltinKind> kind = builtinKind(types);
    if (!kind || types.complexCount > 1) {
        return std::nullopt;
    }
    const ast::QualType type = context.builtin(*kind);
    return types.complexCount > 0 ? context.complexOf(type) : type;
}

bool isTagComplete(const ast::Decl* tag) {
    if (const auto* record = ast::dyn_cast<ast::RecordDecl>(tag)) {
        return record->isComplete();
    }
    return ast::cast<ast::EnumDecl>(tag)->isComplete();
}

// "struct S", "union U" or "enum E", for messages.
std::string tagName(const ast::Decl* tag) {
    std::string_view keyword = "enum";
    if (const auto* record = ast::dyn_cast<ast::RecordDecl>(tag)) {
        keyword = record->isUnion() ? "union" : "struct";
    }
    return std::format("{} {}", keyword, tag->name().text());
}

// Follows typedefs to the type they stand for, keeping the qualifiers.
ast::QualType desugarTypedefs(ast::QualType type) {
    while (!type.isNull()) {
        const auto* typedefType = ast::dyn_cast<ast::TypedefType>(type.type());
        if (typedefType == nullptr) {
            break;
        }
        type = typedefType->decl()->underlying().withQualifiers(type.qualifiers());
    }
    return type;
}

// Whether a declaration of this type declares a function. Besides typedefs, it looks through `typeof(f)` with a
// function f, as in the Linux kernel's `extern typeof(f) f;`; sema resolves other typeofs only later.
bool isFunctionType(ast::QualType type) {
    while (true) {
        type = desugarTypedefs(type);
        if (type.isNull()) {
            return false;
        }
        const auto* typeOf = ast::dyn_cast<ast::TypeofType>(type.type());
        if (typeOf == nullptr || typeOf->expression() == nullptr) {
            return ast::isa<ast::FunctionType>(type.type());
        }
        const auto* reference = ast::dyn_cast<ast::DeclRefExpr>(ast::ignoreParens(typeOf->expression()));
        const auto* function = reference != nullptr ? ast::dyn_cast<ast::FunctionDecl>(reference->decl()) : nullptr;
        if (function == nullptr) {
            return false;
        }
        type = function->type();
    }
}

bool isVoidType(ast::QualType type) {
    type = desugarTypedefs(type);
    if (type.isNull()) {
        return false;
    }
    const auto* builtin = ast::dyn_cast<ast::BuiltinType>(type.type());
    return builtin != nullptr && builtin->builtinKind() == BuiltinKind::Void;
}

bool isQualifier(TokenKind kind) {
    return kind == TokenKind::KwConst || kind == TokenKind::KwVolatile || kind == TokenKind::KwRestrict
           || kind == TokenKind::KwAtomic;
}

void addQualifier(ast::Qualifiers& qualifiers, TokenKind kind) {
    switch (kind) {
    case TokenKind::KwConst:
        qualifiers.isConst = true;
        break;
    case TokenKind::KwVolatile:
        qualifiers.isVolatile = true;
        break;
    case TokenKind::KwRestrict:
        qualifiers.isRestrict = true;
        break;
    case TokenKind::KwAtomic:
        qualifiers.isAtomic = true;
        break;
    default:
        NITWIT_UNREACHABLE("not a qualifier");
    }
}

std::optional<ast::StorageClass> storageClass(TokenKind kind) {
    switch (kind) {
    case TokenKind::KwExtern:
        return ast::StorageClass::Extern;
    case TokenKind::KwStatic:
        return ast::StorageClass::Static;
    case TokenKind::KwAuto:
        return ast::StorageClass::Auto;
    case TokenKind::KwRegister:
        return ast::StorageClass::Register;
    default:
        return std::nullopt;
    }
}

// An attribute's name without GNU's leading and trailing "__": `__aligned__` is `aligned`.
std::string_view attributeName(std::string_view name) {
    if (name.size() > 2 && name.starts_with("__")) {
        name.remove_prefix(2);
    }
    if (name.size() > 2 && name.ends_with("__")) {
        name.remove_suffix(2);
    }
    return name;
}

} // namespace

// ---- Classification of tokens ----

bool Parser::startsTypeName(const lex::Token& token) const {
    switch (token.kind) {
    case TokenKind::KwVoid:
    case TokenKind::KwChar:
    case TokenKind::KwShort:
    case TokenKind::KwInt:
    case TokenKind::KwLong:
    case TokenKind::KwFloat:
    case TokenKind::KwDouble:
    case TokenKind::KwSigned:
    case TokenKind::KwUnsigned:
    case TokenKind::KwBool:
    case TokenKind::KwComplex:
    case TokenKind::KwImaginary:
    case TokenKind::KwInt128:
    case TokenKind::KwFloat128:
    case TokenKind::KwFloat32:
    case TokenKind::KwFloat64:
    case TokenKind::KwFloat32x:
    case TokenKind::KwFloat64x:
    case TokenKind::KwStruct:
    case TokenKind::KwUnion:
    case TokenKind::KwEnum:
    case TokenKind::KwTypeof:
    case TokenKind::KwConst:
    case TokenKind::KwVolatile:
    case TokenKind::KwRestrict:
    case TokenKind::KwAtomic:
        return true;
    case TokenKind::Identifier:
        return isTypedefName(token);
    default:
        return false;
    }
}

// Declaration specifiers other than attributes and __extension__, which may also start other constructs.
bool Parser::startsDeclaration(const lex::Token& token) const {
    switch (token.kind) {
    case TokenKind::KwTypedef:
    case TokenKind::KwExtern:
    case TokenKind::KwStatic:
    case TokenKind::KwAuto:
    case TokenKind::KwRegister:
    case TokenKind::KwThreadLocal:
    case TokenKind::KwInline:
    case TokenKind::KwNoreturn:
    case TokenKind::KwAlignas:
    case TokenKind::KwAutoType:
        return true;
    default:
        return startsTypeName(token);
    }
}

// ---- Declaration specifiers (C17 6.7.1-6.7.5) ----

DeclSpec Parser::parseDeclarationSpecifiers(SpecifierContext context) {
    DeclSpec spec;
    spec.begin = token().location;
    TypeSpecifiers types;
    bool anySpecifier = false;
    while (parseDeclarationSpecifier(spec, types, context)) {
        anySpecifier = true;
    }
    finishTypeSpecifiers(spec, types, anySpecifier, context);
    return spec;
}

bool Parser::parseDeclarationSpecifier(DeclSpec& spec, TypeSpecifiers& types, SpecifierContext context) {
    const lex::Token& current = token();
    const TokenKind kind = current.kind;
    const auto addNamed = [&](ast::QualType type) {
        ++types.namedCount;
        types.named = type;
        types.lastLocation = current.location;
    };
    switch (kind) {
    case TokenKind::KwTypedef:
    case TokenKind::KwExtern:
    case TokenKind::KwStatic:
    case TokenKind::KwAuto:
    case TokenKind::KwRegister:
        setStorageClass(spec, current, context);
        break;
    case TokenKind::KwThreadLocal:
        spec.isThreadLocal = true;
        break;
    case TokenKind::KwInline:
        spec.isInline = true;
        break;
    case TokenKind::KwNoreturn:
        spec.isNoreturn = true;
        break;
    case TokenKind::KwAutoType:
        spec.isAutoType = true;
        break;
    case TokenKind::KwExtension:
        break;
    case TokenKind::KwAttribute:
        parseAttributes(spec.attributes);
        return true;
    case TokenKind::KwAlignas:
        parseAlignas(spec);
        return true;
    case TokenKind::KwStruct:
        addNamed(parseTagSpecifier(spec, context, TagKind::Struct));
        return true;
    case TokenKind::KwUnion:
        addNamed(parseTagSpecifier(spec, context, TagKind::Union));
        return true;
    case TokenKind::KwEnum:
        addNamed(parseTagSpecifier(spec, context, TagKind::Enum));
        return true;
    case TokenKind::KwTypeof:
        addNamed(parseTypeof());
        return true;
    case TokenKind::KwImaginary:
        fail(current.location, "imaginary types are not supported");
    case TokenKind::KwAtomic:
        if (peek(1).is(TokenKind::LeftParen)) {
            // _Atomic(T), the type specifier (C17 6.7.2.4).
            consume();
            consume();
            ast::Qualifiers atomic;
            atomic.isAtomic = true;
            addNamed(parseTypeName().withQualifiers(atomic));
            expect(TokenKind::RightParen);
            return true;
        }
        addQualifier(spec.qualifiers, kind);
        break;
    case TokenKind::Identifier:
        // A typedef name is a type specifier only if there is no other one; `unsigned T;` declares T.
        if (types.any() || spec.isAutoType || !isTypedefName(current)) {
            return false;
        }
        addNamed(context_.typedefType(ast::cast<ast::TypedefDecl>(lookupOrdinary(current.identifier))));
        break;
    default:
        if (isQualifier(kind)) {
            addQualifier(spec.qualifiers, kind);
            break;
        }
        unsigned* counter = keywordCounter(types, kind);
        if (counter == nullptr) {
            return false;
        }
        if (kind == TokenKind::KwFloat128) {
            warning(current.location,
                    std::format("'{}' is approximated by 'long double'", lex::spellingOf(sources_, current)));
        }
        ++*counter;
        types.lastLocation = current.location;
        break;
    }
    consume();
    return true;
}

void Parser::setStorageClass(DeclSpec& spec, const lex::Token& keyword, SpecifierContext context) {
    if (spec.isTypedef || spec.storage != ast::StorageClass::None) {
        fail(keyword.location, "multiple storage classes in declaration specifiers");
    }
    if (context == SpecifierContext::Member || context == SpecifierContext::TypeName) {
        fail(keyword.location, std::format("'{}' is not allowed here", lex::spellingOf(sources_, keyword)));
    }
    if (keyword.is(TokenKind::KwTypedef)) {
        spec.isTypedef = true;
        return;
    }
    spec.storage = *storageClass(keyword.kind);
    if ((context == SpecifierContext::Parameter || context == SpecifierContext::KnrParameter)
        && spec.storage != ast::StorageClass::Register) {
        fail(keyword.location, "invalid storage class for a parameter");
    }
}

void Parser::finishTypeSpecifiers(DeclSpec& spec, const TypeSpecifiers& types, bool anySpecifier,
                                  SpecifierContext context) {
    const std::optional<ast::QualType> type = specifiedType(context_, types);
    if (!type || (spec.isAutoType && !type->isNull())) {
        fail(types.lastLocation, "invalid combination of type specifiers");
    }
    spec.type = *type;
    if (!spec.type.isNull() || spec.isAutoType) {
        return;
    }
    // Implicit int (C89), which GCC still accepts with a warning. Only an external declaration may omit all
    // specifiers, as in `main() { ... }`.
    const bool declaratorFollows = at(TokenKind::Identifier) || at(TokenKind::Star) || at(TokenKind::LeftParen);
    if (!anySpecifier && (context != SpecifierContext::File || !declaratorFollows)) {
        if (at(TokenKind::Identifier)) {
            failUnknownTypeName(token());
        }
        failExpected(context == SpecifierContext::TypeName ? "type name"
                     : context == SpecifierContext::File   ? "declaration"
                                                           : "declaration specifiers");
    }
    warning(spec.begin, "type defaults to 'int'");
    spec.type = context_.builtin(BuiltinKind::Int);
}

// `struct S`, `union U { ... }`, `enum E { ... }`, and anonymous definitions.
ast::QualType Parser::parseTagSpecifier(DeclSpec& spec, SpecifierContext context, TagKind kind) {
    const DepthGuard guard(*this);
    const lex::Token& keyword = consume();
    std::vector<ast::Attribute> attributes;
    parseAttributes(attributes);
    Symbol name;
    SourceLocation location = keyword.location;
    std::vector<ast::Attribute> nameAttributes;
    if (at(TokenKind::Identifier)) {
        name = token().identifier;
        location = token().location;
        consume();
        parseAttributes(nameAttributes);
    }
    const bool isDefinition = at(TokenKind::LeftBrace);
    if (name.empty() && !isDefinition) {
        failExpected("identifier or '{'");
    }
    // C17 6.7.2.3p6-7: a definition and `struct S;` declare the tag in the current scope.
    const bool declaresTag =
        isDefinition
        || (at(TokenKind::Semicolon) && (context == SpecifierContext::File || context == SpecifierContext::Block));
    ast::Decl* tag = resolveTag(kind, name, location, declaresTag);
    if (isDefinition) {
        if (isTagComplete(tag)) {
            fail(location, std::format("redefinition of '{}'", tagName(tag)));
        }
        parseTagBody(tag);
        attributes.insert(attributes.end(), nameAttributes.begin(), nameAttributes.end());
        parseAttributes(attributes);
        tagDefinitions_.push_back(tag);
        spec.definesTag = true;
        addAttributes(tag, attributes);
    } else {
        // As in GCC (c_parser_struct_or_union_specifier), a specifier that does not define the type ignores the
        // attributes after its keyword, and the attributes after its tag belong to the declaration: in
        // `struct S __attribute__((aligned(64))) member;` they align the member, not struct S.
        spec.attributes.insert(spec.attributes.end(), nameAttributes.begin(), nameAttributes.end());
    }
    spec.tag = tag;
    if (auto* record = ast::dyn_cast<ast::RecordDecl>(tag)) {
        return context_.recordType(record);
    }
    return context_.enumType(ast::cast<ast::EnumDecl>(tag));
}

// The tag entity a specifier refers to (C17 6.7.2.3): with `declaresTag` the one of the current scope, otherwise the
// visible one; a new one if there is none (p8). An enum used before its definition is a GNU extension.
ast::Decl* Parser::resolveTag(TagKind kind, Symbol name, SourceLocation location, bool declaresTag) {
    if (ast::Decl* existing = name.empty() ? nullptr : lookupTag(name, declaresTag)) {
        const auto* record = ast::dyn_cast<ast::RecordDecl>(existing);
        const bool matches = kind == TagKind::Enum ? ast::isa<ast::EnumDecl>(existing)
                                                   : record != nullptr && record->isUnion() == (kind == TagKind::Union);
        if (!matches) {
            fail(location,
                 std::format("use of '{}' with tag type that does not match previous declaration", name.text()));
        }
        return existing;
    }
    ast::Decl* tag = nullptr;
    if (kind == TagKind::Enum) {
        tag = context_.create<ast::EnumDecl>(name, location);
    } else {
        tag = context_.create<ast::RecordDecl>(name, location, kind == TagKind::Union);
    }
    if (!name.empty()) {
        bindTag(tag);
    }
    return tag;
}

// The members or enumerators in braces. The tag is completed at the closing brace, where `#pragma pack` applies.
void Parser::parseTagBody(ast::Decl* tag) {
    auto* record = ast::dyn_cast<ast::RecordDecl>(tag);
    std::vector<ast::FieldDecl*> fields;
    std::vector<ast::EnumConstantDecl*> constants;
    consume(); // {
    if (record != nullptr) {
        fields = parseRecordMembers();
    } else {
        constants = parseEnumerators();
    }
    const SourceLocation end = expect(TokenKind::RightBrace).location;
    if (isTagComplete(tag)) {
        // `struct S { struct S { ... } inner; }`: the inner definition completed S first.
        fail(end, std::format("nested redefinition of '{}'", tagName(tag)));
    }
    if (record == nullptr) {
        ast::cast<ast::EnumDecl>(tag)->complete(context_.copy<ast::EnumConstantDecl*>(constants));
        return;
    }
    for (std::size_t i = 0; i < fields.size(); ++i) {
        fields[i]->setParent(record, static_cast<std::uint32_t>(i));
    }
    record->complete(context_.copy<ast::FieldDecl*>(fields));
    record->setMaxFieldAlignment(packAlignment_);
}

// A `_Static_assert` among the members is parsed and dropped: a record has no place for it, and the programs NITWIT
// validates compile, so it holds.
std::vector<ast::FieldDecl*> Parser::parseRecordMembers() {
    std::vector<ast::FieldDecl*> fields;
    while (!at(TokenKind::RightBrace)) {
        if (tryConsume(TokenKind::Semicolon)) {
            continue; // GNU: an extra semicolon
        }
        if (at(TokenKind::KwStaticAssert)) {
            (void)parseStaticAssert();
            continue;
        }
        parseMemberDeclaration(fields);
    }
    return fields;
}

void Parser::parseMemberDeclaration(std::vector<ast::FieldDecl*>& fields) {
    const SourceLocation begin = token().location;
    const DeclSpec spec = parseDeclarationSpecifiers(SpecifierContext::Member);
    if (tryConsume(TokenKind::Semicolon)) {
        // C17 6.7.2.1p13: an anonymous struct or union. A tagged one without a declarator declares nothing.
        const auto* record = ast::dyn_cast<ast::RecordDecl>(spec.tag);
        if (record == nullptr || !record->name().empty() || !spec.definesTag) {
            return warning(begin, "declaration does not declare anything");
        }
        auto* field = context_.create<ast::FieldDecl>(Symbol(), record->location(), spec.baseType(), nullptr);
        addAttributes(field, spec.attributes);
        fields.push_back(field);
        return;
    }
    while (true) {
        Declarator declarator;
        if (!at(TokenKind::Colon)) {
            declarator = parseDeclarator(DeclaratorMode::Named);
        }
        ast::Expr* bitWidth = nullptr;
        if (tryConsume(TokenKind::Colon)) {
            bitWidth = parseConditionalExpression();
        }
        parseDeclaratorTail(declarator);
        const SourceLocation location = declarator.name.empty() ? begin : declarator.nameLocation;
        auto* field = context_.create<ast::FieldDecl>(declarator.name, location,
                                                      buildType(spec.baseType(), declarator.chunks), bitWidth);
        addAttributes(field, spec.attributes);
        addAttributes(field, declarator.attributes);
        fields.push_back(field);
        if (!tryConsume(TokenKind::Comma)) {
            break;
        }
    }
    if (!at(TokenKind::RightBrace)) { // GNU: the last `;` may be missing
        expect(TokenKind::Semicolon);
    }
}

std::vector<ast::EnumConstantDecl*> Parser::parseEnumerators() {
    if (at(TokenKind::RightBrace)) {
        fail(token().location, "empty enum is invalid");
    }
    std::vector<ast::EnumConstantDecl*> constants;
    while (!at(TokenKind::RightBrace)) {
        if (!at(TokenKind::Identifier)) {
            failExpected("identifier");
        }
        const lex::Token& name = consume();
        std::vector<ast::Attribute> attributes;
        parseAttributes(attributes);
        ast::Expr* initializer = nullptr;
        if (tryConsume(TokenKind::Equal)) {
            initializer = parseConditionalExpression();
        }
        auto* constant = context_.create<ast::EnumConstantDecl>(name.identifier, name.location, initializer);
        addAttributes(constant, attributes);
        // An enumeration constant is in scope right after its enumerator (C17 6.2.1p7).
        bindOrdinary(constant, false);
        constants.push_back(constant);
        if (!tryConsume(TokenKind::Comma)) {
            break;
        }
    }
    return constants;
}

// GNU `typeof(expression)` and `typeof(type-name)`.
ast::QualType Parser::parseTypeof() {
    consume();
    expect(TokenKind::LeftParen);
    ast::QualType type;
    if (startsTypeName(token())) {
        type = parseTypeName();
    } else {
        type = context_.typeofExpression(parseExpression(), false);
    }
    expect(TokenKind::RightParen);
    return type;
}

// `_Alignas(N)` becomes the attribute `aligned(N)`, and `_Alignas(T)` becomes `aligned(_Alignof(T))` (C17 6.7.5p3).
void Parser::parseAlignas(DeclSpec& spec) {
    const lex::Token& keyword = consume();
    expect(TokenKind::LeftParen);
    ast::Expr* argument = nullptr;
    if (startsTypeName(token())) {
        const ast::QualType type = parseTypeName();
        argument = context_.create<ast::SizeOfAlignOfExpr>(SourceRange{keyword.location, token().range().end},
                                                           ast::SizeOfAlignOfExpr::Operator::AlignOf, type);
    } else {
        argument = parseConditionalExpression();
    }
    expect(TokenKind::RightParen);
    ast::Expr* const arguments[] = {argument};
    spec.attributes.push_back(ast::Attribute{
        .name = intern("aligned"), .location = keyword.location, .arguments = context_.copy<ast::Expr*>(arguments)});
}

// ---- GNU attributes ----

void Parser::parseAttributes(std::vector<ast::Attribute>& out) {
    while (at(TokenKind::KwAttribute)) {
        consume();
        expect(TokenKind::LeftParen);
        expect(TokenKind::LeftParen);
        while (!at(TokenKind::RightParen)) {
            if (tryConsume(TokenKind::Comma)) {
                continue;
            }
            const lex::Token& name = token();
            if (!name.is(TokenKind::Identifier) && !lex::isKeyword(name.kind)) {
                failExpected("attribute name");
            }
            consume();
            std::vector<ast::Expr*> arguments;
            if (tryConsume(TokenKind::LeftParen)) {
                while (!at(TokenKind::RightParen)) {
                    arguments.push_back(parseAttributeArgument());
                    if (!tryConsume(TokenKind::Comma)) {
                        break;
                    }
                }
                expect(TokenKind::RightParen);
            }
            out.push_back(ast::Attribute{.name = intern(attributeName(lex::spellingOf(sources_, name))),
                                         .location = name.location,
                                         .arguments = context_.copy<ast::Expr*>(arguments)});
            if (!tryConsume(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RightParen);
        expect(TokenKind::RightParen);
    }
}

// An identifier argument, as in `mode(__QI__)` or `format(printf, 1, 2)`, need not be declared.
ast::Expr* Parser::parseAttributeArgument() {
    if (at(TokenKind::Identifier) && (peek(1).is(TokenKind::Comma) || peek(1).is(TokenKind::RightParen))) {
        const lex::Token& name = consume();
        auto* decl = ast::dyn_cast<ast::ValueDecl>(lookupOrdinary(name.identifier));
        return context_.create<ast::DeclRefExpr>(name.range(), name.identifier, decl);
    }
    return parseAssignmentExpression();
}

void Parser::addAttributes(ast::Decl* decl, std::span<const ast::Attribute> attributes) {
    if (attributes.empty()) {
        return;
    }
    std::vector<ast::Attribute> all(decl->attributes().begin(), decl->attributes().end());
    all.insert(all.end(), attributes.begin(), attributes.end());
    decl->setAttributes(context_.copy<ast::Attribute>(all));
}

// ---- Declarators (C17 6.7.6) ----

Declarator Parser::parseDeclarator(DeclaratorMode mode) {
    Declarator declarator;
    parseDeclaratorInto(declarator, mode);
    return declarator;
}

// The chunks of a nested declarator come first, then the suffixes, then the pointers: in `*(*f)[3]`, f is a pointer
// to an array of pointers.
void Parser::parseDeclaratorInto(Declarator& declarator, DeclaratorMode mode) {
    const DepthGuard guard(*this);
    parseAttributes(declarator.attributes);
    std::vector<DeclaratorChunk> pointers;
    while (tryConsume(TokenKind::Star)) {
        DeclaratorChunk pointer;
        pointer.kind = DeclaratorChunk::Kind::Pointer;
        parsePointerQualifiers(pointer.qualifiers, declarator.attributes);
        pointers.push_back(std::move(pointer));
    }
    parseDirectDeclarator(declarator, mode);
    std::ranges::move(pointers | std::views::reverse, std::back_inserter(declarator.chunks));
}

void Parser::parseDirectDeclarator(Declarator& declarator, DeclaratorMode mode) {
    if (at(TokenKind::Identifier) && mode != DeclaratorMode::Abstract) {
        declarator.name = token().identifier;
        declarator.nameLocation = token().location;
        consume();
    } else if (at(TokenKind::LeftParen) && startsNestedDeclarator(mode)) {
        consume();
        parseDeclaratorInto(declarator, mode);
        expect(TokenKind::RightParen);
    } else if (mode == DeclaratorMode::Named) {
        failExpected("identifier or '('");
    }
    while (true) {
        if (at(TokenKind::LeftSquare)) {
            parseArraySuffix(declarator);
        } else if (at(TokenKind::LeftParen)) {
            parseFunctionSuffix(declarator);
        } else {
            break;
        }
    }
}

// Where a declarator may be abstract, `(` starts a parameter list if a type or `)` follows (C17 6.7.7p2).
bool Parser::startsNestedDeclarator(DeclaratorMode mode) const {
    if (mode == DeclaratorMode::Named) {
        return true;
    }
    const lex::Token& next = peek(1);
    if (next.is(TokenKind::RightParen) || next.is(TokenKind::Ellipsis)) {
        return false;
    }
    return next.is(TokenKind::KwAttribute) || !startsDeclaration(next);
}

void Parser::parsePointerQualifiers(ast::Qualifiers& qualifiers, std::vector<ast::Attribute>& attributes) {
    while (true) {
        if (isQualifier(token().kind)) {
            addQualifier(qualifiers, token().kind);
            consume();
        } else if (at(TokenKind::KwAttribute)) {
            parseAttributes(attributes);
        } else {
            return;
        }
    }
}

void Parser::parseArraySuffix(Declarator& declarator) {
    consume(); // [
    DeclaratorChunk array;
    array.kind = DeclaratorChunk::Kind::Array;
    // `static` and qualifiers are only allowed in parameters (C17 6.7.6.3p7); the qualifiers then apply to the
    // pointer the parameter is adjusted to.
    while (true) {
        if (isQualifier(token().kind)) {
            addQualifier(array.qualifiers, token().kind);
            consume();
        } else if (!tryConsume(TokenKind::KwStatic)) {
            break;
        }
    }
    if (at(TokenKind::Star) && peek(1).is(TokenKind::RightSquare)) {
        consume(); // `[*]`: a variable length array of unspecified size
    } else if (!at(TokenKind::RightSquare)) {
        array.size = parseAssignmentExpression();
    }
    expect(TokenKind::RightSquare);
    declarator.chunks.push_back(std::move(array));
}

void Parser::parseFunctionSuffix(Declarator& declarator) {
    consume(); // (
    DeclaratorChunk function;
    function.kind = DeclaratorChunk::Kind::Function;
    if (tryConsume(TokenKind::RightParen)) {
        declarator.chunks.push_back(std::move(function)); // `()` says nothing about the parameters
        return;
    }
    if (at(TokenKind::Identifier) && !isTypedefName(token())) {
        // A K&R identifier list.
        while (true) {
            if (!at(TokenKind::Identifier)) {
                failExpected("identifier");
            }
            if (peek(1).is(TokenKind::Identifier) || peek(1).is(TokenKind::Star)) {
                failUnknownTypeName(token()); // `void f(foo x)` with an undeclared foo
            }
            const lex::Token& name = consume();
            function.identifiers.push_back({name.identifier, name.location});
            if (!tryConsume(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RightParen);
        declarator.chunks.push_back(std::move(function));
        return;
    }

    function.hasPrototype = true;
    const std::size_t mark = tagDefinitions_.size();
    pushScope(ScopeKind::Prototype);
    while (true) {
        if (tryConsume(TokenKind::Ellipsis)) {
            function.isVariadic = true;
            break;
        }
        function.parameters.push_back(parseParameterDeclaration());
        if (!tryConsume(TokenKind::Comma)) {
            break;
        }
    }
    // Kept for a definition, whose body sees the parameters, tags and enumeration constants of the list.
    function.scope = std::make_unique<Scope>(std::move(scopes_.back()));
    scopes_.pop_back();
    tagDefinitions_.resize(mark);
    expect(TokenKind::RightParen);

    // `(void)` declares no parameters (C17 6.7.6.3p10).
    const auto isVoid = [](const ast::VarDecl* parameter) {
        return parameter->name().empty() && isVoidType(parameter->type());
    };
    if (std::ranges::any_of(function.parameters, isVoid)) {
        if (function.parameters.size() != 1 || function.isVariadic) {
            fail(function.parameters.front()->location(), "'void' must be the only parameter");
        }
        function.parameters.clear();
    }
    declarator.chunks.push_back(std::move(function));
}

ast::VarDecl* Parser::parseParameterDeclaration() {
    const SourceLocation begin = token().location;
    const DeclSpec spec = parseDeclarationSpecifiers(SpecifierContext::Parameter);
    Declarator declarator = parseDeclarator(DeclaratorMode::Either);
    parseDeclaratorTail(declarator);
    const SourceLocation location = declarator.name.empty() ? begin : declarator.nameLocation;
    auto* parameter = context_.create<ast::VarDecl>(declarator.name, location, parameterType(spec, declarator),
                                                    spec.storage, false, true);
    addAttributes(parameter, spec.attributes);
    addAttributes(parameter, declarator.attributes);
    bindOrdinary(parameter, false);
    return parameter;
}

// GNU asm labels (`int f(void) __asm__("g");`) are ignored; attributes after the declarator apply to the entity.
void Parser::parseDeclaratorTail(Declarator& declarator) {
    while (true) {
        if (at(TokenKind::KwAsm)) {
            consume();
            expect(TokenKind::LeftParen);
            if (!at(TokenKind::StringLiteral)) {
                failExpected("string literal");
            }
            (void)parseStringLiteral();
            expect(TokenKind::RightParen);
        } else if (at(TokenKind::KwAttribute)) {
            parseAttributes(declarator.attributes);
        } else {
            return;
        }
    }
}

ast::QualType Parser::parseTypeName() {
    const DepthGuard guard(*this); // `_Atomic(_Atomic(...))` and `typeof(typeof(...))` nest through here
    // Tags defined in a type name are reachable through the type only; they belong to no declaration list.
    const std::size_t mark = tagDefinitions_.size();
    const DeclSpec spec = parseDeclarationSpecifiers(SpecifierContext::TypeName);
    Declarator declarator = parseDeclarator(DeclaratorMode::Abstract);
    parseAttributes(declarator.attributes);
    tagDefinitions_.resize(mark);
    return buildType(spec.baseType(), declarator.chunks);
}

// ---- Types and entities ----

ast::QualType Parser::buildType(ast::QualType base, std::span<const DeclaratorChunk> chunks) {
    ast::QualType type = base;
    for (const DeclaratorChunk& chunk : chunks | std::views::reverse) {
        switch (chunk.kind) {
        case DeclaratorChunk::Kind::Pointer:
            type = context_.pointerTo(type).withQualifiers(chunk.qualifiers);
            break;
        case DeclaratorChunk::Kind::Array:
            type = context_.arrayOf(type, chunk.size);
            break;
        case DeclaratorChunk::Kind::Function: {
            // The parameters of a K&R definition are not part of its type (C17 6.7.6.3p14).
            std::vector<ast::QualType> parameters;
            if (chunk.hasPrototype) {
                parameters.reserve(chunk.parameters.size());
                for (const ast::VarDecl* parameter : chunk.parameters) {
                    parameters.push_back(parameter->type());
                }
            }
            type = context_.functionType(type, parameters, chunk.isVariadic, chunk.hasPrototype);
            break;
        }
        }
    }
    return type;
}

// A parameter of array type is adjusted to a pointer, with the qualifiers from inside the brackets, and one of function
// type to a pointer to the function (C17 6.7.6.3p7-8).
ast::QualType Parser::parameterType(const DeclSpec& spec, const Declarator& declarator) {
    if (spec.isAutoType) {
        fail(spec.begin, "'__auto_type' is not allowed here");
    }
    const std::span<const DeclaratorChunk> chunks = declarator.chunks;
    if (!chunks.empty() && chunks.front().kind == DeclaratorChunk::Kind::Array) {
        return context_.pointerTo(buildType(spec.baseType(), chunks.subspan(1)))
            .withQualifiers(chunks.front().qualifiers);
    }
    const ast::QualType type = buildType(spec.baseType(), chunks);
    const ast::QualType desugared = desugarTypedefs(type);
    if (const auto* array = ast::dyn_cast<ast::ArrayType>(desugared.type())) {
        return context_.pointerTo(array->element().withQualifiers(desugared.qualifiers()));
    }
    if (ast::isa<ast::FunctionType>(desugared.type())) {
        return context_.pointerTo(type);
    }
    return type;
}

ast::Decl* Parser::declare(const DeclSpec& spec, const Declarator& declarator) {
    if (spec.isAutoType && !declarator.chunks.empty()) {
        fail(declarator.nameLocation, "'__auto_type' requires a plain identifier as declarator");
    }
    const ast::QualType type = spec.isAutoType ? ast::QualType() : buildType(spec.baseType(), declarator.chunks);
    const Symbol name = declarator.name;
    const SourceLocation location = declarator.nameLocation;

    ast::Decl* decl = nullptr;
    if (spec.isTypedef) {
        if (spec.isAutoType) {
            fail(location, "'__auto_type' is not allowed in a typedef");
        }
        decl = context_.create<ast::TypedefDecl>(name, location, type);
        bindOrdinary(decl, false);
    } else if (isFunctionType(type)) {
        if (spec.storage == ast::StorageClass::Auto || spec.storage == ast::StorageClass::Register) {
            fail(location, std::format("invalid storage class for function '{}'", name.text()));
        }
        auto* function = context_.create<ast::FunctionDecl>(name, location, type, spec.storage);
        if (spec.isInline) {
            function->setInline();
        }
        if (spec.isNoreturn) {
            function->setNoreturn();
        }
        if (declarator.isFunction()) {
            function->setParameters(context_.copy<ast::VarDecl*>(declarator.chunks.front().parameters));
        }
        decl = function;
        bindOrdinary(decl, true);
    } else {
        if (spec.isInline || spec.isNoreturn) {
            fail(location,
                 std::format("variable '{}' declared '{}'", name.text(), spec.isInline ? "inline" : "_Noreturn"));
        }
        const bool isFileScope = atFileScope();
        auto* var = context_.create<ast::VarDecl>(name, location, type, spec.storage, isFileScope, false);
        if (spec.isThreadLocal) {
            var->setThreadLocal();
        }
        decl = var;
        bindOrdinary(decl, isFileScope || spec.storage == ast::StorageClass::Extern);
    }
    addAttributes(decl, spec.attributes);
    addAttributes(decl, declarator.attributes);
    return decl;
}

void Parser::parseInitDeclarators(const DeclSpec& spec, Declarator first, std::vector<ast::Decl*>& out) {
    Declarator declarator = std::move(first);
    while (true) {
        // The name is in scope from the end of its declarator, so the initializer can refer to it (C17 6.2.1p7).
        ast::Decl* decl = declare(spec, declarator);
        auto* var = ast::dyn_cast<ast::VarDecl>(decl);
        if (at(TokenKind::Equal)) {
            if (var == nullptr) {
                fail(token().location, std::format("'{}' cannot be initialized", decl->name().text()));
            }
            consume();
            ast::Expr* initializer = parseInitializer();
            var->setInitializer(initializer);
            if (spec.isAutoType) {
                var->setType(context_.typeofExpression(initializer, true).withQualifiers(spec.qualifiers));
            }
        } else if (spec.isAutoType) {
            fail(declarator.nameLocation, "'__auto_type' requires an initializer");
        }
        out.push_back(decl);
        if (!tryConsume(TokenKind::Comma)) {
            return;
        }
        declarator = parseDeclarator(DeclaratorMode::Named);
        parseDeclaratorTail(declarator);
    }
}

void Parser::finishEmptyDeclaration(const DeclSpec& spec) {
    if (spec.tag == nullptr) {
        warning(spec.begin, "declaration does not declare anything");
    }
}

ast::FunctionDecl* Parser::parseFunctionDefinition(const DeclSpec& spec, Declarator& declarator) {
    if (spec.isTypedef) {
        fail(declarator.nameLocation, "a function definition cannot be a typedef");
    }
    if (currentFunction_ != nullptr) {
        fail(declarator.nameLocation, "nested functions are not supported");
    }
    // The outermost block of the body is the scope of the parameters (C17 6.2.1p4): the parameter list's scope, or
    // for a K&R definition the scope of its declaration list.
    DeclaratorChunk& chunk = declarator.chunks.front();
    Scope body;
    if (!chunk.identifiers.empty()) {
        pushScope(ScopeKind::Block);
        chunk.parameters = parseKnrParameters(chunk);
        body = std::move(scopes_.back());
        scopes_.pop_back();
    } else if (chunk.scope != nullptr) {
        body = std::move(*chunk.scope);
    }
    body.kind = ScopeKind::Block;

    auto* function = ast::cast<ast::FunctionDecl>(declare(spec, declarator));
    for (const ast::FunctionDecl* previous = function->previous(); previous != nullptr;
         previous = previous->previous()) {
        if (previous->body() != nullptr) {
            fail(function->location(), std::format("redefinition of '{}'", function->name().text()));
        }
    }

    scopes_.push_back(std::move(body));
    currentFunction_ = function;
    functionName_ = nullptr;
    functionLabels_ = LabelScope{};
    function->setBody(parseCompoundStatement(false));
    resolveGotos(functionLabels_);
    popScope();
    currentFunction_ = nullptr;
    return function;
}

// The parameter declarations of a K&R definition, `int f(a, b) int a; char *b; { ... }`, in the scope of the body:
// each parameter is in scope after its declarator, so `int f(n, a) int n; int a[n]; { ... }` works. Parameters
// without a declaration are int (C89 6.7.1).
std::vector<ast::VarDecl*> Parser::parseKnrParameters(const DeclaratorChunk& chunk) {
    std::vector<ast::VarDecl*> parameters(chunk.identifiers.size(), nullptr);
    while (!at(TokenKind::LeftBrace)) {
        const std::size_t mark = tagDefinitions_.size();
        const DeclSpec spec = parseDeclarationSpecifiers(SpecifierContext::KnrParameter);
        while (true) {
            Declarator declarator = parseDeclarator(DeclaratorMode::Named);
            parseDeclaratorTail(declarator);
            const auto found =
                std::ranges::find(chunk.identifiers, declarator.name, &DeclaratorChunk::Identifier::name);
            if (found == chunk.identifiers.end()) {
                fail(declarator.nameLocation,
                     std::format("declaration for parameter '{}' but no such parameter", declarator.name.text()));
            }
            ast::VarDecl*& parameter = parameters[static_cast<std::size_t>(found - chunk.identifiers.begin())];
            if (parameter != nullptr) {
                fail(declarator.nameLocation, std::format("redefinition of parameter '{}'", declarator.name.text()));
            }
            parameter = context_.create<ast::VarDecl>(declarator.name, declarator.nameLocation,
                                                      parameterType(spec, declarator), spec.storage, false, true);
            addAttributes(parameter, spec.attributes);
            addAttributes(parameter, declarator.attributes);
            bindOrdinary(parameter, false);
            if (!tryConsume(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::Semicolon);
        tagDefinitions_.resize(mark);
    }
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (parameters[i] == nullptr) {
            const DeclaratorChunk::Identifier& identifier = chunk.identifiers[i];
            warning(identifier.location, std::format("type of '{}' defaults to 'int'", identifier.name.text()));
            parameters[i] =
                context_.create<ast::VarDecl>(identifier.name, identifier.location, context_.builtin(BuiltinKind::Int),
                                              ast::StorageClass::None, false, true);
            bindOrdinary(parameters[i], false);
        }
    }
    return parameters;
}

ast::StaticAssertDecl* Parser::parseStaticAssert() {
    const lex::Token& keyword = consume();
    expect(TokenKind::LeftParen);
    ast::Expr* condition = parseConditionalExpression();
    ast::Expr* message = nullptr;
    if (tryConsume(TokenKind::Comma)) {
        if (!at(TokenKind::StringLiteral)) {
            failExpected("string literal");
        }
        message = parseStringLiteral();
    }
    expect(TokenKind::RightParen);
    expect(TokenKind::Semicolon);
    return context_.create<ast::StaticAssertDecl>(keyword.location, condition, message);
}

void Parser::flushTagDefinitions(std::size_t mark, std::vector<ast::Decl*>& out) {
    out.insert(out.end(), tagDefinitions_.begin() + static_cast<std::ptrdiff_t>(mark), tagDefinitions_.end());
    tagDefinitions_.resize(mark);
}

} // namespace nitwit::parse::detail
