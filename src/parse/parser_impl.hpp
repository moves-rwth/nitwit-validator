#pragma once

// The parser's state, shared by its source files; private to the module. parser.cpp has the token handling, pragmas
// and the translation unit, scope.cpp the name binding, declarations.cpp, expressions.cpp and statements.cpp the
// grammar, constraint.cpp the witness constraints.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "ast/stmt.hpp"
#include "lex/token.hpp"
#include "support/diagnostics.hpp"
#include "support/interner.hpp"
#include "support/source_manager.hpp"

namespace nitwit::parse {
struct ConstraintScope;
} // namespace nitwit::parse

namespace nitwit::parse::detail {

using lex::TokenKind;

/// Thrown after an error has been reported; parseTranslationUnit and parseConstraint catch it.
struct ParseError {};

/// The name of the identifier token that constraint.cpp makes of `\result`.
inline constexpr std::string_view resultName = "\\result";

/// The labels of a function, or of a block that declares local labels with GNU `__label__`.
struct LabelScope {
    std::unordered_map<Symbol, ast::LabelStmt*> labels; // null for a declared local label not defined yet
    std::vector<ast::GotoStmt*> gotos;                  // resolved when the label scope ends
};

enum class ScopeKind : std::uint8_t { File, Block, Prototype };

/// A scope of C17 6.2.1 with its ordinary identifiers and tags.
struct Scope {
    ScopeKind kind = ScopeKind::Block;
    std::unordered_map<Symbol, ast::Decl*> ordinary; // VarDecl, FunctionDecl, EnumConstantDecl, TypedefDecl
    std::unordered_map<Symbol, ast::Decl*> tags;     // RecordDecl, EnumDecl
    std::optional<LabelScope> localLabels;
};

/// Where declaration specifiers occur. It decides which specifiers are allowed and whether `int` may be omitted.
enum class SpecifierContext : std::uint8_t { File, Block, Member, Parameter, KnrParameter, TypeName };

struct DeclSpec {
    SourceLocation begin;
    ast::StorageClass storage = ast::StorageClass::None;
    bool isTypedef = false;
    bool isThreadLocal = false;
    bool isInline = false;
    bool isNoreturn = false;
    bool isAutoType = false; // GNU __auto_type; `type` is null
    ast::QualType type;      // without `qualifiers`
    ast::Qualifiers qualifiers;
    std::vector<ast::Attribute> attributes;
    ast::Decl* tag = nullptr; // the struct, union or enum the specifiers name, if any
    bool definesTag = false;  // the specifiers contain the tag's definition

    [[nodiscard]] ast::QualType baseType() const { return type.withQualifiers(qualifiers); }
};

/// The type specifiers of one declaration, keywords counted, for the combinations of C17 6.7.2p2.
struct TypeSpecifiers {
    unsigned voidCount = 0;
    unsigned boolCount = 0;
    unsigned charCount = 0;
    unsigned intCount = 0;
    unsigned floatCount = 0;
    unsigned doubleCount = 0;
    unsigned int128Count = 0;
    unsigned shortCount = 0;
    unsigned longCount = 0;
    unsigned signedCount = 0;
    unsigned unsignedCount = 0;
    unsigned complexCount = 0;
    unsigned namedCount = 0; // typedef names, tags, typeof, _Atomic(T)
    ast::QualType named;
    SourceLocation lastLocation;

    [[nodiscard]] unsigned keywordCount() const {
        return voidCount + boolCount + charCount + intCount + floatCount + doubleCount + int128Count + shortCount
               + longCount + signedCount + unsignedCount + complexCount;
    }
    [[nodiscard]] bool any() const { return keywordCount() + namedCount > 0; }
};

struct DeclaratorChunk {
    enum class Kind : std::uint8_t { Pointer, Array, Function };

    struct Identifier {
        Symbol name;
        SourceLocation location;
    };

    Kind kind = Kind::Pointer;
    ast::Qualifiers qualifiers; // of the pointer, or inside the brackets of an array parameter
    ast::Expr* size = nullptr;  // Array: null for `[]` and `[*]`
    bool hasPrototype = false;  // Function
    bool isVariadic = false;    // Function
    std::vector<ast::VarDecl*> parameters;
    std::vector<Identifier> identifiers; // Function: the identifier list of a K&R declarator
    /// Function with a prototype: the scope of the parameter list, which becomes the outermost scope of the body if
    /// this is a definition (C17 6.2.1p4); it also holds tags and enumeration constants declared in the list.
    std::unique_ptr<Scope> scope;
};

struct Declarator {
    Symbol name; // empty for an abstract declarator
    SourceLocation nameLocation;
    /// The derivations, the one closest to the name first: `*a[3]` gives Array, Pointer (an array of pointers).
    std::vector<DeclaratorChunk> chunks;
    std::vector<ast::Attribute> attributes;

    [[nodiscard]] bool isFunction() const {
        return !chunks.empty() && chunks.front().kind == DeclaratorChunk::Kind::Function;
    }
};

enum class DeclaratorMode : std::uint8_t { Named, Abstract, Either };

enum class TagKind : std::uint8_t { Struct, Union, Enum };

/// A label of a chain like `case 1: case 2: L:`, parsed before the statement it labels.
struct PendingLabel {
    TokenKind kind = TokenKind::Identifier; // KwCase, KwDefault or Identifier
    SourceLocation begin;
    ast::Expr* value = nullptr;
    ast::Expr* rangeEnd = nullptr;
    Symbol name;
    std::size_t switchIndex = 0; // case and default: the switch in Parser::switches_
    std::size_t caseSlot = 0;    // case: the slot in that switch's cases
};

class Parser {
public:
    Parser(std::span<const lex::Token> tokens, ast::AstContext& context, const SourceManager& sources,
           DiagnosticEngine& diagnostics);

    /// Throws ParseError after reporting the first error.
    ast::TranslationUnit* parseTranslationUnit();

    /// Parses the tokens as a witness constraint at `scope` (constraint.cpp). Throws ParseError after reporting the
    /// first error.
    ast::Expr* parseConstraint(const ConstraintScope& scope);

private:
    struct SwitchContext {
        std::vector<ast::CaseStmt*> cases; // a slot per case label, filled once the label's statement is parsed
        ast::DefaultStmt* defaultStmt = nullptr;
        bool hasDefault = false;
    };

    struct PackEntry {
        std::uint64_t alignment;
        std::string id; // `#pragma pack(push, id)`
    };

    // Counts the nesting of the recursive constructs and stops the parser before it exhausts the stack.
    class DepthGuard {
    public:
        explicit DepthGuard(Parser& parser);
        ~DepthGuard() { --parser_.depth_; }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;

    private:
        Parser& parser_;
    };

    // ---- parser.cpp: tokens, diagnostics, pragmas, external declarations ----
    [[nodiscard]] const lex::Token& token() const { return tokens_[pos_]; }
    [[nodiscard]] bool at(TokenKind kind) const { return tokens_[pos_].kind == kind; }
    /// The index of the token after the one at `index`, skipping pragmas; EndOfFile is its own successor.
    [[nodiscard]] std::size_t following(std::size_t index) const;
    /// The token `ahead` tokens after the current one, not counting pragmas.
    [[nodiscard]] const lex::Token& peek(std::size_t ahead) const;
    const lex::Token& consume();
    bool tryConsume(TokenKind kind);
    const lex::Token& expect(TokenKind kind);
    void skipPragmas();
    [[nodiscard]] SourceRange rangeFrom(SourceLocation begin) const { return {begin, previousEnd_}; }
    [[nodiscard]] std::string describe(const lex::Token& token) const;
    [[noreturn]] void fail(SourceLocation location, std::string message);
    [[noreturn]] void failExpected(std::string_view what);
    void warning(SourceLocation location, std::string message);
    [[nodiscard]] Symbol intern(std::string_view text) { return interner_.intern(text); }

    void handlePragma(const lex::Token& pragma);
    void handlePackPragma(std::string_view arguments, SourceLocation location);
    void popPackAlignment(std::string_view id, SourceLocation location);
    void handleWeakPragma(std::string_view arguments, SourceLocation location);
    void applyWeakPragmas();

    void parseExternalDeclaration(std::vector<ast::Decl*>& out);
    void skipFileScopeAsm();

    // ---- scope.cpp: scopes and name binding ----
    void pushScope(ScopeKind kind);
    void popScope();
    [[nodiscard]] bool atFileScope() const { return scopes_.back().kind == ScopeKind::File; }
    [[nodiscard]] ast::Decl* lookupOrdinary(Symbol name) const;
    [[nodiscard]] bool isTypedefName(const lex::Token& token) const;
    [[nodiscard]] ast::Decl* lookupTag(Symbol name, bool currentScopeOnly) const;
    void bindTag(ast::Decl* tag);
    /// Binds a declaration's name in the current scope and links it to the previous declaration of the same entity.
    void bindOrdinary(ast::Decl* decl, bool hasLinkage);
    void declarePredefinedTypedef(std::string_view name, ast::BuiltinKind kind);
    ast::FunctionDecl* declareImplicitFunction(const lex::Token& name);
    [[nodiscard]] LabelScope& labelScopeFor(Symbol name);
    void defineLabel(ast::LabelStmt* label);
    void resolveGotos(LabelScope& scope);

    // ---- declarations.cpp ----
    [[nodiscard]] bool startsTypeName(const lex::Token& token) const;
    [[nodiscard]] bool startsDeclaration(const lex::Token& token) const;
    DeclSpec parseDeclarationSpecifiers(SpecifierContext context);
    /// Parses one declaration specifier into `spec` and `types`; false if the current token is none.
    bool parseDeclarationSpecifier(DeclSpec& spec, TypeSpecifiers& types, SpecifierContext context);
    void setStorageClass(DeclSpec& spec, const lex::Token& keyword, SpecifierContext context);
    void finishTypeSpecifiers(DeclSpec& spec, const TypeSpecifiers& types, bool anySpecifier, SpecifierContext context);
    ast::QualType parseTagSpecifier(DeclSpec& spec, SpecifierContext context, TagKind kind);
    ast::Decl* resolveTag(TagKind kind, Symbol name, SourceLocation location, bool declaresTag);
    void parseTagBody(ast::Decl* tag);
    std::vector<ast::FieldDecl*> parseRecordMembers();
    void parseMemberDeclaration(std::vector<ast::FieldDecl*>& fields);
    std::vector<ast::EnumConstantDecl*> parseEnumerators();
    ast::QualType parseTypeof();
    void parseAlignas(DeclSpec& spec);
    void parseAttributes(std::vector<ast::Attribute>& out);
    ast::Expr* parseAttributeArgument();
    void addAttributes(ast::Decl* decl, std::span<const ast::Attribute> attributes);

    Declarator parseDeclarator(DeclaratorMode mode);
    void parseDeclaratorInto(Declarator& declarator, DeclaratorMode mode);
    void parseDirectDeclarator(Declarator& declarator, DeclaratorMode mode);
    [[nodiscard]] bool startsNestedDeclarator(DeclaratorMode mode) const;
    void parsePointerQualifiers(ast::Qualifiers& qualifiers, std::vector<ast::Attribute>& attributes);
    void parseArraySuffix(Declarator& declarator);
    void parseFunctionSuffix(Declarator& declarator);
    ast::VarDecl* parseParameterDeclaration();
    void parseDeclaratorTail(Declarator& declarator);
    ast::QualType parseTypeName();

    [[nodiscard]] ast::QualType buildType(ast::QualType base, std::span<const DeclaratorChunk> chunks);
    [[nodiscard]] ast::QualType parameterType(const DeclSpec& spec, const Declarator& declarator);
    /// Declares the entity of one declarator: a typedef, a function or a variable.
    ast::Decl* declare(const DeclSpec& spec, const Declarator& declarator);
    /// The declarators of a declaration whose first declarator is already parsed, up to the `;`.
    void parseInitDeclarators(const DeclSpec& spec, Declarator first, std::vector<ast::Decl*>& out);
    void finishEmptyDeclaration(const DeclSpec& spec);
    ast::FunctionDecl* parseFunctionDefinition(const DeclSpec& spec, Declarator& declarator);
    std::vector<ast::VarDecl*> parseKnrParameters(const DeclaratorChunk& chunk);
    ast::StaticAssertDecl* parseStaticAssert();
    /// Moves the tag definitions made since `mark` into a declaration list.
    void flushTagDefinitions(std::size_t mark, std::vector<ast::Decl*>& out);

    // ---- expressions.cpp ----
    ast::Expr* parseExpression();
    ast::Expr* parseAssignmentExpression();
    ast::Expr* parseConditionalExpression();
    ast::Expr* parseBinaryExpression(int minPrecedence);
    ast::Expr* parseCastExpression();
    ast::Expr* parseUnaryExpression();
    ast::Expr* parseSizeOfAlignOf();
    ast::Expr* parsePostfixSuffixes(ast::Expr* expr);
    ast::Expr* parsePrimaryExpression();
    ast::Expr* parseIdentifierExpression();
    ast::Expr* functionNameLiteral(const lex::Token& name);
    ast::StringLiteralExpr* parseStringLiteral();
    ast::Expr* parseCompoundLiteral(SourceLocation begin, ast::QualType type);
    ast::Expr* parseStatementExpression();
    ast::Expr* parseGenericSelection();
    ast::Expr* parseBuiltinVaArg();
    ast::Expr* parseBuiltinOffsetof();
    ast::Expr* parseBuiltinTypesCompatible();
    ast::Expr* parseBuiltinChooseExpr();
    ast::Expr* parseInitializer();
    ast::InitListExpr* parseInitializerList();
    std::vector<ast::Designator> parseDesignation();

    // ---- statements.cpp ----
    ast::Stmt* parseStatement();
    ast::Stmt* parseSubstatement();
    ast::CompoundStmt* parseCompoundStatement(bool opensScope);
    ast::Stmt* parseLabeledStatement();
    PendingLabel parseLabel();
    ast::Stmt* buildLabeledStatement(std::span<const PendingLabel> labels, ast::Stmt* stmt);
    ast::Stmt* parseDeclarationStatement(SourceLocation begin, std::vector<ast::Attribute> attributes);
    ast::Stmt* parseExpressionStatement();
    ast::Stmt* parseIfStatement();
    ast::Stmt* parseWhileStatement();
    ast::Stmt* parseDoStatement();
    ast::Stmt* parseForStatement();
    ast::Stmt* parseSwitchStatement();
    ast::Stmt* parseGotoStatement();
    ast::Stmt* parseReturnStatement();
    ast::Stmt* parseAsmStatement();
    void parseAsmOperands();
    void parseLocalLabels();
    [[nodiscard]] bool startsLabel() const;
    /// Whether the statement at the current token, after any `__extension__`, is a declaration.
    [[nodiscard]] bool startsDeclarationStatement() const;
    [[noreturn]] void failUnknownTypeName(const lex::Token& name);

    // ---- constraint.cpp ----
    /// Reports an error if a constraint is being parsed; `construct` names what it must not contain.
    void rejectInConstraint(SourceLocation location, std::string_view construct);

    std::span<const lex::Token> tokens_;
    std::size_t pos_ = 0;
    SourceLocation previousEnd_; // the end of the last consumed token
    ast::AstContext& context_;
    const SourceManager& sources_;
    DiagnosticEngine& diagnostics_;
    StringInterner& interner_;
    std::size_t depth_ = 0;
    std::size_t maxDepth_; // lower for constraints, which are parsed on the caller's stack
    /// While parsing a constraint, `\result` is allowed; side effects and undeclared identifiers are not.
    bool inConstraint_ = false;

    std::vector<Scope> scopes_;
    /// The latest declaration with linkage of each name, wherever it was declared (C17 6.2.2).
    std::unordered_map<Symbol, ast::ValueDecl*> externals_;
    std::vector<ast::FunctionDecl*> implicitDecls_; // since the start of the current external declaration
    std::vector<ast::Decl*> tagDefinitions_;        // not yet added to a declaration list

    ast::FunctionDecl* currentFunction_ = nullptr;
    ast::StringLiteralExpr* functionName_ = nullptr; // `__func__` of the current function, created at its first use
    LabelScope functionLabels_;
    std::vector<SwitchContext> switches_;
    /// The switches below this index enclose the current statement expression; its case labels cannot belong to them.
    std::size_t switchBarrier_ = 0;
    std::size_t loopDepth_ = 0;

    std::uint64_t packAlignment_ = 0; // `#pragma pack`; 0 means none
    std::vector<PackEntry> packStack_;
    std::vector<Symbol> weakNames_; // `#pragma weak`
};

} // namespace nitwit::parse::detail
