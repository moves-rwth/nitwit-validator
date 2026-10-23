// buildProgram: lowers every function body to a flat instruction list and plans the functions, the objects with
// static storage duration and the string literals of the program.

#include <algorithm>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ast/casting.hpp"
#include "interp/interpreter.hpp"
#include "interp/program.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

using ast::cast;
using ast::dyn_cast;
using ast::isa;

/// Whether the expression is an integer literal other than 0, perhaps in parentheses.
bool isNonZeroLiteral(const ast::Expr* expr) {
    while (const auto* paren = dyn_cast<ast::ParenExpr>(expr)) {
        expr = paren->inner();
    }
    const auto* literal = dyn_cast<ast::IntegerLiteralExpr>(expr);
    return literal != nullptr && literal->value().value != 0;
}

/// Whether an instruction neither has an effect nor leaves the instructions after it: a statement event, or a branch
/// whose condition is missing (`for (;;)`) or a nonzero integer literal, so that it never jumps.
bool isWithoutEffectOrExit(const Instruction& instruction) {
    switch (instruction.op) {
    case OpCode::Statement:
        return true;
    case OpCode::Branch:
        return instruction.expr == nullptr || isNonZeroLiteral(instruction.expr);
    default:
        return false;
    }
}

struct BuiltinName {
    std::string_view name;
    Builtin builtin;
    BuiltinOperation operation = BuiltinOperation::Add;
};

/// The builtins the evaluator implements; they take precedence over the library.
constexpr BuiltinName builtinNames[] = {
    {"__VERIFIER_assume", Builtin::Assume},
    {"__VERIFIER_nondet_memory", Builtin::NondetMemory},
    {"__builtin_va_start", Builtin::VaStart},
    {"__builtin_va_end", Builtin::VaEnd},
    {"__builtin_va_copy", Builtin::VaCopy},
    {"__builtin_constant_p", Builtin::ConstantP},
    {"__builtin_object_size", Builtin::ObjectSize},
    {"__builtin_dynamic_object_size", Builtin::ObjectSize},
    {"__builtin_expect", Builtin::PassThrough},
    {"__builtin_expect_with_probability", Builtin::PassThrough},
    {"__builtin_assume_aligned", Builtin::PassThrough},
    {"alloca", Builtin::Alloca},
    {"__builtin_alloca", Builtin::Alloca},
    {"__builtin_alloca_with_align", Builtin::Alloca},
    {"__builtin_add_overflow", Builtin::Overflow, BuiltinOperation::Add},
    {"__builtin_sub_overflow", Builtin::Overflow, BuiltinOperation::Sub},
    {"__builtin_mul_overflow", Builtin::Overflow, BuiltinOperation::Mul},
    {"__builtin_add_overflow_p", Builtin::OverflowP, BuiltinOperation::Add},
    {"__builtin_sub_overflow_p", Builtin::OverflowP, BuiltinOperation::Sub},
    {"__builtin_mul_overflow_p", Builtin::OverflowP, BuiltinOperation::Mul},
    {"__atomic_load_n", Builtin::AtomicLoad},
    {"__atomic_store_n", Builtin::AtomicStore},
    {"__atomic_exchange_n", Builtin::AtomicExchange},
    {"__atomic_compare_exchange_n", Builtin::AtomicCompareExchange},
    {"__atomic_load", Builtin::AtomicLoadGeneric},
    {"__atomic_store", Builtin::AtomicStoreGeneric},
    {"__atomic_exchange", Builtin::AtomicExchangeGeneric},
    {"__atomic_compare_exchange", Builtin::AtomicCompareExchangeGeneric},
    {"__atomic_fetch_add", Builtin::AtomicFetchOp, BuiltinOperation::Add},
    {"__atomic_fetch_sub", Builtin::AtomicFetchOp, BuiltinOperation::Sub},
    {"__atomic_fetch_and", Builtin::AtomicFetchOp, BuiltinOperation::And},
    {"__atomic_fetch_or", Builtin::AtomicFetchOp, BuiltinOperation::Or},
    {"__atomic_fetch_xor", Builtin::AtomicFetchOp, BuiltinOperation::Xor},
    {"__atomic_fetch_nand", Builtin::AtomicFetchOp, BuiltinOperation::Nand},
    {"__atomic_add_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Add},
    {"__atomic_sub_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Sub},
    {"__atomic_and_fetch", Builtin::AtomicOpFetch, BuiltinOperation::And},
    {"__atomic_or_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Or},
    {"__atomic_xor_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Xor},
    {"__atomic_nand_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Nand},
    {"__sync_fetch_and_add", Builtin::AtomicFetchOp, BuiltinOperation::Add},
    {"__sync_fetch_and_sub", Builtin::AtomicFetchOp, BuiltinOperation::Sub},
    {"__sync_fetch_and_and", Builtin::AtomicFetchOp, BuiltinOperation::And},
    {"__sync_fetch_and_or", Builtin::AtomicFetchOp, BuiltinOperation::Or},
    {"__sync_fetch_and_xor", Builtin::AtomicFetchOp, BuiltinOperation::Xor},
    {"__sync_fetch_and_nand", Builtin::AtomicFetchOp, BuiltinOperation::Nand},
    {"__sync_add_and_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Add},
    {"__sync_sub_and_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Sub},
    {"__sync_and_and_fetch", Builtin::AtomicOpFetch, BuiltinOperation::And},
    {"__sync_or_and_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Or},
    {"__sync_xor_and_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Xor},
    {"__sync_nand_and_fetch", Builtin::AtomicOpFetch, BuiltinOperation::Nand},
    {"__sync_bool_compare_and_swap", Builtin::SyncBoolCompareSwap},
    {"__sync_val_compare_and_swap", Builtin::SyncValCompareSwap},
    {"__sync_lock_test_and_set", Builtin::AtomicExchange},
    {"__sync_lock_release", Builtin::SyncLockRelease},
    {"__sync_synchronize", Builtin::AtomicNoOp},
    {"__atomic_test_and_set", Builtin::AtomicTestAndSet},
    {"__atomic_clear", Builtin::AtomicClear},
    {"__atomic_thread_fence", Builtin::AtomicNoOp},
    {"__atomic_signal_fence", Builtin::AtomicNoOp},
    {"__atomic_always_lock_free", Builtin::AtomicLockFree},
    {"__atomic_is_lock_free", Builtin::AtomicLockFree},
};

/// The name a GNU `alias("target")` attribute names, from the code units of its string literal; empty if there is no
/// such attribute.
std::string aliasTarget(std::span<const ast::Attribute> attributes) {
    for (const ast::Attribute& attribute : attributes) {
        if (attribute.name.text() != "alias" || attribute.arguments.size() != 1) {
            continue;
        }
        if (const auto* target = dyn_cast<ast::StringLiteralExpr>(attribute.arguments.front())) {
            std::string name;
            for (const std::uint32_t unit : target->codeUnits()) {
                name.push_back(static_cast<char>(unit));
            }
            return name;
        }
    }
    return {};
}

/// Whether the type has a variable length array that a declaration of this type must evaluate: through arrays and
/// pointers, but not through typedefs and typeof, whose sizes were fixed where they were declared (C17 6.7.8p3).
bool hasOwnVariableSizes(ast::QualType type) {
    const ast::Type* current = type.type();
    while (true) {
        if (const auto* array = dyn_cast<ast::ArrayType>(current)) {
            if (array->sizeKind() == ast::ArrayType::SizeKind::Variable) {
                return true;
            }
            current = array->element().type();
        } else if (const auto* pointer = dyn_cast<ast::PointerType>(current)) {
            current = pointer->pointee().type();
        } else {
            return false;
        }
    }
}

/// Whether the object of this type is a variable length array, which is allocated at its declaration.
bool isVariableLengthArray(ast::QualType type) {
    const auto* array = dyn_cast<ast::ArrayType>(desugared(type));
    for (; array != nullptr; array = dyn_cast<ast::ArrayType>(desugared(array->element()))) {
        if (array->sizeKind() == ast::ArrayType::SizeKind::Variable) {
            return true;
        }
    }
    return false;
}

/// Whether sizeof is defined for the type: not an array of unknown size, not an incomplete struct or union. Such
/// objects can be declared (`extern int a[];`) and get no accessible bytes.
bool isComplete(ast::QualType type) {
    const ast::Type* canonical = desugared(type);
    if (const auto* array = dyn_cast<ast::ArrayType>(canonical)) {
        return array->sizeKind() != ast::ArrayType::SizeKind::Incomplete && isComplete(array->element());
    }
    if (const auto* record = dyn_cast<ast::RecordType>(canonical)) {
        return record->decl()->layout() != nullptr;
    }
    return true;
}

/// The size of an object: sizeof its type, or more for a struct whose flexible array member is initialized (GNU),
/// since the object then extends to the end of the member's initializer.
std::uint64_t objectSize(ast::AstContext& context, ast::QualType type, const ast::Expr* initializer) {
    if (!isComplete(type)) {
        return 0;
    }
    const std::uint64_t size = sema::sizeOf(context, type);
    const auto* record = dyn_cast<ast::RecordType>(desugared(type));
    const auto* list = initializer != nullptr ? dyn_cast<ast::InitListExpr>(ast::ignoreParens(initializer)) : nullptr;
    if (record == nullptr || list == nullptr || record->decl()->isUnion() || record->decl()->fields().empty()
        || list->elements().size() < record->decl()->fields().size()) {
        return size;
    }
    const ast::FieldDecl* last = record->decl()->fields().back();
    const auto* array = dyn_cast<ast::ArrayType>(desugared(last->type()));
    if (array == nullptr || array->sizeKind() != ast::ArrayType::SizeKind::Incomplete) {
        return size;
    }
    const ast::Expr* memberInitializer = list->elements()[last->index()];
    if (isa<ast::ImplicitZeroExpr>(memberInitializer) || !isComplete(memberInitializer->type())) {
        return size;
    }
    const std::uint64_t end =
        sema::layoutOf(record->decl()).fieldOffsets[last->index()] + sema::sizeOf(context, memberInitializer->type());
    return std::max(size, end);
}

/// The layout of an object declared with `type`; `requestedAlignment` from `aligned` or _Alignas.
ObjectLayout objectLayout(ast::AstContext& context, ast::QualType type, const ast::Expr* initializer,
                          std::uint64_t requestedAlignment) {
    if (isVariableLengthArray(type)) {
        return {.size = 0, .alignment = 1, .isVariableLength = true};
    }
    if (!isComplete(type)) {
        return {.size = 0, .alignment = std::max<std::uint64_t>(requestedAlignment, 1), .isVariableLength = false};
    }
    return {.size = objectSize(context, type, initializer),
            .alignment = std::max(sema::alignOf(context, type), requestedAlignment),
            .isVariableLength = false};
}

class Lowering {
public:
    Lowering(ast::AstContext& context, const ast::TranslationUnit& unit)
        : program_(std::make_shared<Program>(context, unit)) {}

    std::expected<std::shared_ptr<const Program>, Error> run() {
        for (const ast::Decl* decl : program_->unit.decls) {
            if (const auto* var = dyn_cast<ast::VarDecl>(decl)) {
                staticObject(var);
            } else if (const auto* function = dyn_cast<ast::FunctionDecl>(decl)) {
                (void)functionEntity(function);
                if (function->body() != nullptr) {
                    lowerFunction(function);
                }
            }
        }
        resolveFunctions();
        resolveGlobals();
        const auto main = program_->functionsByName.find(program_->context.interner().intern("main"));
        if (main == program_->functionsByName.end()
            || program_->functions[main->second].kind != FunctionKind::Defined) {
            return std::unexpected(Error{"the program has no definition of 'main'"});
        }
        program_->main = main->second;
        return program_;
    }

private:
    // ---- Functions and objects with static storage ----

    FunctionId functionEntity(const ast::FunctionDecl* decl) {
        if (const auto found = program_->functionIds.find(decl); found != program_->functionIds.end()) {
            return found->second;
        }
        // Every declaration of a function with the same name denotes the same function (C17 6.2.2).
        auto [byName, isNew] =
            program_->functionsByName.try_emplace(decl->name(), static_cast<FunctionId>(program_->functions.size()));
        if (isNew) {
            program_->functions.push_back(FunctionEntity{.name = decl->name(), .decl = decl});
        }
        FunctionEntity& entity = program_->functions[byName->second];
        if (entity.decl->body() == nullptr) {
            entity.decl = decl; // the definition wins; otherwise the last declaration, which has the merged type
        }
        program_->functionIds.emplace(decl, byName->second);
        return byName->second;
    }

    /// Registers a declaration of an object with static storage and walks its initializer.
    void staticObject(const ast::VarDecl* decl) {
        const GlobalId id = globalEntity(decl);
        GlobalEntity& entity = program_->globals[id];
        entity.decl = decl;
        // A definition: a file-scope declaration without `extern` (a tentative definition, C17 6.9.2), a static
        // local, or any declaration with an initializer.
        if (decl->storage() != ast::StorageClass::Extern || decl->initializer() != nullptr) {
            isDefinedByProgram_[id] = true;
        }
        if (decl->initializer() != nullptr) {
            entity.initialized = decl;
            // Initializers of static objects are evaluated before main, outside any frame.
            FunctionCode* body = std::exchange(body_, nullptr);
            walkInitializer(decl->initializer());
            body_ = body;
        }
        walkType(decl->type());
    }

    GlobalId globalEntity(const ast::VarDecl* decl) {
        if (const auto found = program_->globalIds.find(decl); found != program_->globalIds.end()) {
            return found->second;
        }
        GlobalId id = static_cast<GlobalId>(program_->globals.size());
        bool isNew = true;
        if (hasLinkage(decl)) {
            const auto [byName, inserted] = program_->globalsByName.try_emplace(decl->name(), id);
            id = byName->second;
            isNew = inserted;
        }
        if (isNew) {
            program_->globals.push_back(GlobalEntity{.name = decl->name(), .decl = decl});
            isDefinedByProgram_.push_back(false);
        }
        program_->globalIds.emplace(decl, id);
        return id;
    }

    void resolveFunctions() {
        for (FunctionEntity& entity : program_->functions) {
            const std::string_view name = entity.name.text();
            if (entity.decl->body() != nullptr) {
                entity.kind = FunctionKind::Defined;
                continue;
            }
            if (const std::string target = aliasTarget(attributesOf(entity.decl)); !target.empty()) {
                const auto found = program_->functionsByName.find(program_->context.interner().intern(target));
                if (found != program_->functionsByName.end()) {
                    entity.kind = FunctionKind::Alias;
                    entity.aliasTarget = found->second;
                    continue;
                }
            }
            if (const auto* builtin = std::ranges::find(builtinNames, name, &BuiltinName::name);
                builtin != std::end(builtinNames)) {
                entity.kind = FunctionKind::Builtin;
                entity.builtin = builtin->builtin;
                entity.operation = builtin->operation;
                entity.library = findLibraryFunction(name);
            } else if (name.starts_with("__VERIFIER_nondet_")) {
                entity.kind = FunctionKind::Nondet;
            } else if (const LibraryFunction function = findLibraryFunction(name); function != nullptr) {
                entity.kind = FunctionKind::Library;
                entity.library = function;
            } else if (entity.decl->isWeak()) {
                entity.kind = FunctionKind::WeakMissing;
            } else {
                entity.kind = FunctionKind::Unknown;
            }
        }
        for (const auto& body : program_->bodies) {
            program_->functions[program_->functionIds.at(body->definition)].code = body.get();
        }
    }

    /// Objects the program only declares are the library's if it knows them; otherwise they get zeroed storage like a
    /// definition (a native build would not link).
    void resolveGlobals() {
        for (GlobalId id = 0; id < program_->globals.size(); ++id) {
            GlobalEntity& entity = program_->globals[id];
            if (const std::string target = aliasTarget(attributesOf(entity.decl)); !target.empty()) {
                const auto found = program_->globalsByName.find(program_->context.interner().intern(target));
                if (found != program_->globalsByName.end()) {
                    entity.kind = GlobalEntity::Kind::Alias;
                    entity.aliasTarget = found->second;
                    continue;
                }
            }
            if (!isDefinedByProgram_[id] && libraryObject(entity.name.text(), program_->context.target()).has_value()) {
                entity.kind = GlobalEntity::Kind::LibraryObject;
                continue;
            }
            std::uint64_t alignment = 0;
            for (const ast::VarDecl* decl = entity.decl; decl != nullptr; decl = decl->previous()) {
                alignment = std::max(alignment, decl->requestedAlignment());
            }
            const ast::Expr* initializer = entity.initialized != nullptr ? entity.initialized->initializer() : nullptr;
            entity.layout = objectLayout(program_->context, entity.decl->type(), initializer, alignment);
        }
        for (const ast::CompoundLiteralExpr* literal : program_->fileScopeLiterals) {
            program_->fileScopeLiteralLayouts.push_back(
                objectLayout(program_->context, literal->type(), literal->initializer(), 0));
        }
    }

    /// The attributes of a declaration and of all its previous declarations.
    template <typename Declaration>
    std::vector<ast::Attribute> attributesOf(const Declaration* decl) const {
        std::vector<ast::Attribute> attributes;
        for (; decl != nullptr; decl = decl->previous()) {
            attributes.insert(attributes.end(), decl->attributes().begin(), decl->attributes().end());
        }
        return attributes;
    }

    // ---- Walking expressions: what they refer to and need storage for ----

    void walk(const ast::Expr* expr) {
        if (expr == nullptr) {
            return;
        }
        switch (expr->kind()) {
        case ast::ExprKind::IntegerLiteral:
        case ast::ExprKind::FloatingLiteral:
        case ast::ExprKind::CharacterLiteral:
        case ast::ExprKind::ImplicitZero:
        case ast::ExprKind::TypesCompatible:
        case ast::ExprKind::Result:
            return;
        case ast::ExprKind::StringLiteral:
            return stringLiteral(cast<ast::StringLiteralExpr>(expr));
        case ast::ExprKind::DeclRef:
            return reference(cast<ast::DeclRefExpr>(expr)->decl());
        case ast::ExprKind::Paren:
            return walk(cast<ast::ParenExpr>(expr)->inner());
        case ast::ExprKind::Unary:
            return walk(cast<ast::UnaryExpr>(expr)->operand());
        case ast::ExprKind::Binary: {
            // Left-nested chains (a + b + ... + z) are walked in a loop, so that long ones need no deep recursion.
            const auto* binary = cast<ast::BinaryExpr>(expr);
            while (binary != nullptr) {
                walk(binary->rhs());
                const ast::Expr* lhs = binary->lhs();
                binary = dyn_cast<ast::BinaryExpr>(lhs);
                if (binary == nullptr) {
                    walk(lhs);
                }
            }
            return;
        }
        case ast::ExprKind::Conditional: {
            const auto* conditional = cast<ast::ConditionalExpr>(expr);
            walk(conditional->condition());
            walk(conditional->trueExpr());
            return walk(conditional->falseExpr());
        }
        case ast::ExprKind::Call: {
            const auto* call = cast<ast::CallExpr>(expr);
            walk(call->callee());
            for (const ast::Expr* argument : call->arguments()) {
                walk(argument);
            }
            return;
        }
        case ast::ExprKind::Member:
            return walk(cast<ast::MemberExpr>(expr)->base());
        case ast::ExprKind::ArraySubscript: {
            const auto* subscript = cast<ast::ArraySubscriptExpr>(expr);
            walk(subscript->base());
            return walk(subscript->index());
        }
        case ast::ExprKind::Cast: {
            const auto* castExpr = cast<ast::CastExpr>(expr);
            walkType(castExpr->writtenType());
            return walk(castExpr->operand());
        }
        case ast::ExprKind::ImplicitCast:
            return walk(cast<ast::ImplicitCastExpr>(expr)->operand());
        case ast::ExprKind::CompoundLiteral:
            return compoundLiteral(cast<ast::CompoundLiteralExpr>(expr));
        case ast::ExprKind::InitList:
            return walkInitializer(expr);
        case ast::ExprKind::DesignatedInit:
            return walk(cast<ast::DesignatedInitExpr>(expr)->initializer());
        case ast::ExprKind::SizeOfAlignOf: {
            const auto* size = cast<ast::SizeOfAlignOfExpr>(expr);
            if (size->operand() == nullptr) {
                return walkType(size->typeOperand());
            }
            return walk(size->operand());
        }
        case ast::ExprKind::OffsetOf:
            for (const ast::OffsetOfExpr::Component& component : cast<ast::OffsetOfExpr>(expr)->components()) {
                walk(component.index);
            }
            return;
        case ast::ExprKind::VaArg: {
            const auto* vaArg = cast<ast::VaArgExpr>(expr);
            walkType(vaArg->argumentType());
            return walk(vaArg->list());
        }
        case ast::ExprKind::StatementExpr:
            // Lowered as a region of its function's instructions; its statements are walked then.
            if (statementExpressions_ != nullptr) {
                statementExpressions_->push_back(cast<ast::StatementExpr>(expr));
            }
            return;
        case ast::ExprKind::GenericSelection:
            return walk(cast<ast::GenericSelectionExpr>(expr)->selected());
        case ast::ExprKind::ChooseExpr:
            return walk(cast<ast::ChooseExpr>(expr)->selected());
        }
        NITWIT_UNREACHABLE("unknown expression kind");
    }

    /// An initializer: a string literal that initializes an array directly (not through a decay) needs no storage.
    void walkInitializer(const ast::Expr* initializer) {
        if (initializer == nullptr) {
            return;
        }
        initializer = ast::ignoreParens(initializer);
        if (isa<ast::StringLiteralExpr>(initializer)) {
            return;
        }
        if (const auto* list = dyn_cast<ast::InitListExpr>(initializer)) {
            for (const ast::Expr* element : list->elements()) {
                walkInitializer(element);
            }
            return;
        }
        walk(initializer);
    }

    /// The size expressions of variable length arrays in a type written here (not those behind typedefs).
    void walkType(ast::QualType type) {
        const ast::Type* current = type.type();
        while (current != nullptr) {
            if (const auto* array = dyn_cast<ast::ArrayType>(current)) {
                if (array->sizeKind() == ast::ArrayType::SizeKind::Variable) {
                    walk(array->sizeExpression());
                }
                current = array->element().type();
            } else if (const auto* pointer = dyn_cast<ast::PointerType>(current)) {
                current = pointer->pointee().type();
            } else {
                current = nullptr;
            }
        }
    }

    void reference(const ast::ValueDecl* decl) {
        if (decl == nullptr) {
            return;
        }
        if (const auto* function = dyn_cast<ast::FunctionDecl>(decl)) {
            (void)functionEntity(function);
        } else if (const auto* var = dyn_cast<ast::VarDecl>(decl); var != nullptr && var->hasStaticStorage()) {
            (void)globalEntity(var);
        }
    }

    void stringLiteral(const ast::StringLiteralExpr* literal) {
        const auto id = static_cast<std::uint32_t>(program_->strings.size());
        if (program_->stringIds.try_emplace(literal, id).second) {
            program_->strings.push_back(literal);
        }
    }

    void compoundLiteral(const ast::CompoundLiteralExpr* literal) {
        walkType(literal->writtenType());
        if (literal->isFileScope() || body_ == nullptr) {
            const auto id = static_cast<std::uint32_t>(program_->fileScopeLiterals.size());
            if (program_->fileScopeLiteralIds.try_emplace(literal, id).second) {
                program_->fileScopeLiterals.push_back(literal);
            }
        } else {
            const auto id = static_cast<std::uint32_t>(body_->compoundLiterals.size());
            if (body_->compoundLiteralIds.try_emplace(literal, id).second) {
                body_->compoundLiterals.push_back(literal);
            }
        }
        walkInitializer(literal->initializer());
    }

    // ---- Function bodies ----

    void lowerFunction(const ast::FunctionDecl* definition) {
        auto body = std::make_unique<FunctionCode>();
        body->definition = definition;
        body_ = body.get();
        vlaDepth_ = 0;
        labels_.clear();
        gotos_.clear();
        caseTargets_.clear();
        for (const ast::VarDecl* parameter : definition->parameters()) {
            // The sizes in variably modified parameter types are evaluated on entry (C17 6.9.1p10).
            if (hasOwnVariableSizes(parameter->type())) {
                lowerStatementExpressions([&] { walkType(parameter->type()); });
                emit({.op = OpCode::Declare, .decl = parameter});
            }
        }
        block(definition->body());
        emit({.op = OpCode::FallOff});
        for (const auto& [jump, label] : gotos_) {
            body_->code[jump].target = labels_.at(label);
        }
        markEndlessLoops();
        for (const ast::VarDecl* local : definition->locals()) {
            body_->locals.push_back(
                objectLayout(program_->context, local->type(), nullptr, local->requestedAlignment()));
        }
        for (const ast::CompoundLiteralExpr* literal : body_->compoundLiterals) {
            body_->compoundLiteralLayouts.push_back(
                objectLayout(program_->context, literal->type(), literal->initializer(), 0));
        }
        program_->bodies.push_back(std::move(body));
        body_ = nullptr;
    }

    /// Marks the jumps back over instructions without effect or exit (Instruction::closesEndlessLoop). Once such a jump
    /// is taken, the function runs these instructions for ever: they never leave, and as they change nothing, every
    /// round is the same. Calls, reads (also of volatile objects), declarations and the end of scopes are effects, and
    /// so are the inputs a condition other than a literal could request.
    void markEndlessLoops() {
        std::vector<Instruction>& code = body_->code;
        for (std::uint32_t index = 0; index < code.size(); ++index) {
            Instruction& jump = code[index];
            if (jump.op == OpCode::Jump && jump.target <= index) {
                jump.closesEndlessLoop =
                    std::all_of(code.begin() + jump.target, code.begin() + index, isWithoutEffectOrExit);
            }
        }
    }

    std::uint32_t here() const { return static_cast<std::uint32_t>(body_->code.size()); }

    std::uint32_t emit(Instruction instruction) {
        instruction.vlaDepth = vlaDepth_;
        body_->code.push_back(instruction);
        return here() - 1;
    }

    void patch(std::uint32_t jump) { body_->code[jump].target = here(); }

    void block(const ast::CompoundStmt* compound) {
        const std::uint32_t depth = vlaDepth_;
        for (const ast::Stmt* stmt : compound->body()) {
            statement(stmt);
        }
        endScope(depth);
    }

    /// Ends the variable length arrays declared since `depth` when a block is left at its end.
    void endScope(std::uint32_t depth) {
        if (vlaDepth_ != depth) {
            emit({.op = OpCode::EndScope, .target = depth});
            vlaDepth_ = depth;
        }
    }

    /// A statement in a block, or the body of a loop, `if`, `switch` or label: it reports a statement event unless it
    /// is a block (observer.hpp, StatementEvent).
    void statement(const ast::Stmt* stmt) {
        const std::uint32_t start = here();
        if (!isa<ast::CompoundStmt>(stmt)) {
            emit({.op = OpCode::Statement, .stmt = stmt});
        }
        switch (stmt->kind()) {
        case ast::StmtKind::Compound:
            return block(cast<ast::CompoundStmt>(stmt));
        case ast::StmtKind::Expression: {
            const ast::Expr* expr = cast<ast::ExpressionStmt>(stmt)->expr();
            expressions({expr});
            // The statement that gives the value of the enclosing statement expression keeps it.
            emit({.op = stmt == valueStatement_ ? OpCode::StatementValue : OpCode::Evaluate, .expr = expr});
            return;
        }
        case ast::StmtKind::Declaration:
            return declarations(cast<ast::DeclarationStmt>(stmt));
        case ast::StmtKind::If:
            return ifStatement(cast<ast::IfStmt>(stmt));
        case ast::StmtKind::While:
            return whileStatement(cast<ast::WhileStmt>(stmt));
        case ast::StmtKind::DoWhile:
            return doStatement(cast<ast::DoWhileStmt>(stmt));
        case ast::StmtKind::For:
            return forStatement(cast<ast::ForStmt>(stmt));
        case ast::StmtKind::Switch:
            return switchStatement(cast<ast::SwitchStmt>(stmt));
        case ast::StmtKind::Case: {
            const auto* label = cast<ast::CaseStmt>(stmt);
            const auto [table, index] = caseTargets_.at(label);
            body_->switches[table].cases[index].target = start;
            return statement(label->subStmt());
        }
        case ast::StmtKind::Default: {
            const auto* label = cast<ast::DefaultStmt>(stmt);
            body_->switches[defaultTargets_.at(label)].defaultTarget = start;
            return statement(label->subStmt());
        }
        case ast::StmtKind::Label: {
            const auto* label = cast<ast::LabelStmt>(stmt);
            labels_.emplace(label, start);
            return statement(label->subStmt());
        }
        case ast::StmtKind::Break:
            NITWIT_ASSERT(breaks_ != nullptr, "break outside a loop or switch");
            breaks_->push_back(emit({.op = OpCode::Jump}));
            return;
        case ast::StmtKind::Continue:
            NITWIT_ASSERT(continues_ != nullptr, "continue outside a loop");
            continues_->push_back(emit({.op = OpCode::Jump}));
            return;
        case ast::StmtKind::Return: {
            const ast::Expr* value = cast<ast::ReturnStmt>(stmt)->value();
            expressions({value});
            emit({.op = OpCode::Return, .expr = value});
            return;
        }
        case ast::StmtKind::Goto: {
            const ast::LabelStmt* target = cast<ast::GotoStmt>(stmt)->target();
            NITWIT_ASSERT(target != nullptr, "goto without a resolved label");
            gotos_.emplace_back(emit({.op = OpCode::Jump}), target);
            return;
        }
        case ast::StmtKind::Null:
            return;
        case ast::StmtKind::Asm: {
            // An empty template is a compiler barrier, e.g. __asm__ __volatile__("" ::: "memory").
            const ast::StringLiteralExpr* asmTemplate = cast<ast::AsmStmt>(stmt)->asmTemplate();
            if (asmTemplate != nullptr && !asmTemplate->codeUnits().empty()) {
                emit({.op = OpCode::Unsupported, .stmt = stmt});
            }
            return;
        }
        }
        NITWIT_UNREACHABLE("unknown statement kind");
    }

    void declarations(const ast::DeclarationStmt* declaration) {
        for (const ast::Decl* decl : declaration->decls()) {
            if (const auto* var = dyn_cast<ast::VarDecl>(decl)) {
                if (var->hasStaticStorage()) {
                    staticObject(var);
                    continue;
                }
                lowerStatementExpressions([&] {
                    walkType(var->type());
                    walkInitializer(var->initializer());
                });
                // Also without an initializer: reaching the declaration again makes the object indeterminate
                // (C17 6.2.4p6).
                emit({.op = OpCode::Declare, .decl = var});
                if (isVariableLengthArray(var->type())) {
                    ++vlaDepth_; // allocated by the Declare
                }
            } else if (const auto* typedefDecl = dyn_cast<ast::TypedefDecl>(decl)) {
                if (hasOwnVariableSizes(typedefDecl->underlying())) {
                    lowerStatementExpressions([&] { walkType(typedefDecl->underlying()); });
                    emit({.op = OpCode::Declare, .decl = typedefDecl});
                }
            } else if (const auto* function = dyn_cast<ast::FunctionDecl>(decl)) {
                (void)functionEntity(function);
            }
        }
    }

    void ifStatement(const ast::IfStmt* stmt) {
        expressions({stmt->condition()});
        const std::uint32_t branch = emit({.op = OpCode::Branch,
                                           .construct = ConditionEvent::Construct::If,
                                           .stmt = stmt,
                                           .expr = stmt->condition(),
                                           .location = stmt->location()});
        statement(stmt->thenStmt());
        if (stmt->elseStmt() == nullptr) {
            return patch(branch);
        }
        const std::uint32_t skipElse = emit({.op = OpCode::Jump});
        patch(branch);
        statement(stmt->elseStmt());
        patch(skipElse);
    }

    /// The jumps of `break` and `continue` in a loop body, patched once their targets are known.
    struct LoopJumps {
        std::vector<std::uint32_t> breaks;
        std::vector<std::uint32_t> continues;
    };

    void loopBody(const ast::Stmt* body, LoopJumps& jumps) {
        auto* breaks = std::exchange(breaks_, &jumps.breaks);
        auto* continues = std::exchange(continues_, &jumps.continues);
        statement(body);
        breaks_ = breaks;
        continues_ = continues;
    }

    void patchAll(const std::vector<std::uint32_t>& jumps, std::uint32_t target) {
        for (const std::uint32_t jump : jumps) {
            body_->code[jump].target = target;
        }
    }

    void whileStatement(const ast::WhileStmt* stmt) {
        expressions({stmt->condition()});
        const std::uint32_t condition = emit({.op = OpCode::Branch,
                                              .construct = ConditionEvent::Construct::While,
                                              .stmt = stmt,
                                              .expr = stmt->condition(),
                                              .location = stmt->location()});
        LoopJumps jumps;
        loopBody(stmt->body(), jumps);
        emit({.op = OpCode::Jump, .target = condition});
        patch(condition);
        patchAll(jumps.breaks, here());
        patchAll(jumps.continues, condition);
    }

    void doStatement(const ast::DoWhileStmt* stmt) {
        const std::uint32_t body = here();
        LoopJumps jumps;
        loopBody(stmt->body(), jumps);
        expressions({stmt->condition()});
        const std::uint32_t condition = emit({.op = OpCode::Branch,
                                              .construct = ConditionEvent::Construct::DoWhile,
                                              .stmt = stmt,
                                              .expr = stmt->condition(),
                                              .location = stmt->whileLocation()});
        emit({.op = OpCode::Jump, .target = body});
        patch(condition);
        patchAll(jumps.breaks, here());
        patchAll(jumps.continues, condition);
    }

    void forStatement(const ast::ForStmt* stmt) {
        const std::uint32_t depth = vlaDepth_;
        if (const auto* init = dyn_cast<ast::DeclarationStmt>(stmt->init())) {
            declarations(init);
        } else if (const auto* expression = dyn_cast<ast::ExpressionStmt>(stmt->init())) {
            expressions({expression->expr()});
            emit({.op = OpCode::Evaluate, .expr = expression->expr()});
        }
        expressions({stmt->condition(), stmt->increment()});
        // Without a condition, the condition counts as true and is still reported (observer.hpp).
        const std::uint32_t condition = emit({.op = OpCode::Branch,
                                              .construct = ConditionEvent::Construct::For,
                                              .stmt = stmt,
                                              .expr = stmt->condition(),
                                              .location = stmt->location()});
        LoopJumps jumps;
        loopBody(stmt->body(), jumps);
        const std::uint32_t increment = here();
        if (stmt->increment() != nullptr) {
            emit({.op = OpCode::Evaluate, .expr = stmt->increment()});
        }
        emit({.op = OpCode::Jump, .target = condition});
        patch(condition);
        patchAll(jumps.breaks, here());
        patchAll(jumps.continues, increment);
        endScope(depth);
    }

    void switchStatement(const ast::SwitchStmt* stmt) {
        expressions({stmt->condition()});
        const auto table = static_cast<std::uint32_t>(body_->switches.size());
        SwitchTable& switchTable = body_->switches.emplace_back();
        for (const ast::CaseStmt* label : stmt->cases()) {
            caseTargets_.emplace(label, std::pair(table, static_cast<std::uint32_t>(switchTable.cases.size())));
            switchTable.cases.push_back({.low = label->low(), .high = label->high(), .target = 0, .label = label});
        }
        switchTable.defaultLabel = stmt->defaultStmt();
        if (stmt->defaultStmt() != nullptr) {
            defaultTargets_.emplace(stmt->defaultStmt(), table);
        }
        emit({.op = OpCode::Switch,
              .construct = ConditionEvent::Construct::Switch,
              .index = table,
              .stmt = stmt,
              .expr = stmt->condition(),
              .location = stmt->location()});
        std::vector<std::uint32_t> breaks;
        auto* outerBreaks = std::exchange(breaks_, &breaks);
        statement(stmt->body());
        breaks_ = outerBreaks;
        const std::uint32_t end = here();
        patchAll(breaks, end);
        SwitchTable& finished = body_->switches[table];
        if (finished.defaultLabel == nullptr) {
            finished.defaultTarget = end;
        }
        // Sorted for the binary search at run time; the targets are set, so the positions may change now.
        std::ranges::sort(finished.cases, {}, &SwitchTable::Case::low);
    }

    /// Walks the expressions a statement evaluates. Their statement expressions are lowered right here, behind a jump
    /// that skips them: the evaluator runs them when it reaches them, and break, continue and goto in them see the
    /// loops and labels around the statement.
    void expressions(std::initializer_list<const ast::Expr*> exprs) {
        lowerStatementExpressions([&] {
            for (const ast::Expr* expr : exprs) {
                walk(expr);
            }
        });
    }

    /// Runs `walkAll`, which walks what a statement evaluates (expressions, and the sizes in the types it declares),
    /// and lowers the statement expressions found, as `expressions` does.
    template <typename Walk>
    void lowerStatementExpressions(const Walk& walkAll) {
        std::vector<const ast::StatementExpr*> found;
        auto* outer = std::exchange(statementExpressions_, &found);
        walkAll();
        statementExpressions_ = outer;
        if (found.empty()) {
            return;
        }
        const std::uint32_t skip = emit({.op = OpCode::Jump});
        for (const ast::StatementExpr* statementExpr : found) {
            statementExpression(statementExpr);
        }
        patch(skip);
    }

    /// GNU `({ ... })`: the value is that of StatementExpr::valueStatement(), the last statement if it is an
    /// expression statement (also under labels).
    void statementExpression(const ast::StatementExpr* statementExpr) {
        const std::uint32_t begin = here();
        const std::uint32_t depth = vlaDepth_;
        const ast::ExpressionStmt* outer = std::exchange(valueStatement_, statementExpr->valueStatement());
        for (const ast::Stmt* stmt : statementExpr->body()->body()) {
            statement(stmt);
        }
        valueStatement_ = outer;
        endScope(depth);
        emit({.op = OpCode::EndStatementValue});
        body_->statementExpressions.emplace(statementExpr, InstructionRange{.begin = begin, .end = here()});
    }

    std::shared_ptr<Program> program_;
    std::vector<bool> isDefinedByProgram_; // per global
    FunctionCode* body_ = nullptr;
    std::uint32_t vlaDepth_ = 0;
    std::vector<std::uint32_t>* breaks_ = nullptr;
    std::vector<std::uint32_t>* continues_ = nullptr;
    std::vector<const ast::StatementExpr*>* statementExpressions_ = nullptr;
    const ast::ExpressionStmt* valueStatement_ = nullptr; // of the statement expression being lowered
    std::unordered_map<const ast::LabelStmt*, std::uint32_t> labels_;
    std::vector<std::pair<std::uint32_t, const ast::LabelStmt*>> gotos_;
    std::unordered_map<const ast::CaseStmt*, std::pair<std::uint32_t, std::uint32_t>> caseTargets_;
    std::unordered_map<const ast::DefaultStmt*, std::uint32_t> defaultTargets_;
};

} // namespace

const FunctionEntity* Program::findFunction(const ast::FunctionDecl* decl) const {
    if (const auto found = functionIds.find(decl); found != functionIds.end()) {
        return &functions[found->second];
    }
    if (const auto found = functionsByName.find(decl->name()); found != functionsByName.end()) {
        return &functions[found->second];
    }
    return nullptr;
}

std::optional<GlobalId> Program::findGlobal(const ast::VarDecl* decl) const {
    if (const auto found = globalIds.find(decl); found != globalIds.end()) {
        return found->second;
    }
    if (hasLinkage(decl)) {
        if (const auto found = globalsByName.find(decl->name()); found != globalsByName.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

std::expected<std::shared_ptr<const Program>, Error> buildProgram(const ast::TranslationUnit& unit,
                                                                  ast::AstContext& context) {
    return Lowering(context, unit).run();
}

} // namespace nitwit::interp
