#pragma once

// Internal to the interpreter: the lowered program that buildProgram produces and every run executes. Function bodies
// are flat instruction lists; expressions stay trees of the typed AST. Functions, objects with static storage and
// string literals are numbered, so that a run keeps their addresses in vectors.

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "ast/stmt.hpp"
#include "interp/library.hpp"
#include "interp/observer.hpp"
#include "interp/type_info.hpp"
#include "support/int128.hpp"
#include "support/interner.hpp"
#include "support/source_manager.hpp"

namespace nitwit::interp {

using FunctionId = std::uint32_t;
using GlobalId = std::uint32_t;

enum class OpCode : std::uint8_t {
    Statement,         // reports a StatementEvent for `stmt` and counts a step
    Evaluate,          // evaluates `expr` for its side effects
    Declare,           // `decl`: evaluates the sizes of variable length arrays, allocates them, runs the initializer
                       // or makes the object indeterminate again
    Branch,            // evaluates the condition `expr` (null: true), reports it, and continues at `target` if false
    Jump,              // continues at `target`
    Switch,            // evaluates `expr`, reports it, and continues at the label `switches[index]` selects
    Return,            // returns the value of `expr` (null: no value)
    FallOff,           // the end of a function body: returns without a value
    EndScope,          // the end of a block: ends the variable length arrays above depth `target`
    StatementValue,    // evaluates `expr`, the last statement of a statement expression, and keeps its value
    EndStatementValue, // the end of a statement expression's body
    Unsupported,       // a statement the interpreter cannot execute (inline assembly)
};

struct Instruction {
    OpCode op = OpCode::Statement;
    ConditionEvent::Construct construct = ConditionEvent::Construct::If; // Branch, Switch
    /// Jump: it closes an endless loop without effect, a cycle from `target` to this jump through statement events and
    /// branches on a nonzero integer literal only (`L: goto L;`, `while (1);`, `for (;;) {}`), which never leaves and
    /// never changes the state.
    bool closesEndlessLoop = false;
    std::uint32_t target = 0;
    std::uint32_t index = 0; // Switch: the table
    /// The number of variable length arrays alive before this instruction. A jump to this instruction ends those
    /// above it (C17 6.2.4p7: leaving the scope, or jumping back before the declaration).
    std::uint32_t vlaDepth = 0;
    const ast::Stmt* stmt = nullptr; // Statement, Branch, Switch, Unsupported
    const ast::Expr* expr = nullptr;
    const ast::Decl* decl = nullptr; // Declare: a VarDecl or a TypedefDecl
    SourceLocation location{};       // Branch, Switch: where the witness formats locate the condition
};

/// The jump table of a `switch` (C17 6.8.4.2p5); the cases are sorted by value and do not overlap.
struct SwitchTable {
    struct Case {
        Int128 low = 0;
        Int128 high = 0;
        std::uint32_t target = 0;
        const ast::CaseStmt* label = nullptr;
    };
    std::vector<Case> cases;
    std::uint32_t defaultTarget = 0; // the end of the switch if there is no default label
    const ast::DefaultStmt* defaultLabel = nullptr;
};

/// The instructions of a statement expression's body: [begin, end), the last one is EndStatementValue. Jumps out of
/// the range leave the statement expression (GNU allows break, continue, goto and return in it).
struct InstructionRange {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

/// The size and alignment of an object the program allocates.
struct ObjectLayout {
    std::uint64_t size = 0;
    std::uint64_t alignment = 1;
    bool isVariableLength = false; // a variable length array: allocated at its declaration, with its size then
};

/// The lowered body of a function definition.
struct FunctionCode {
    const ast::FunctionDecl* definition = nullptr;
    std::vector<Instruction> code;
    std::vector<SwitchTable> switches;
    std::unordered_map<const ast::StatementExpr*, InstructionRange> statementExpressions;
    /// The locals (FunctionDecl::locals()), which a frame allocates at entry.
    std::vector<ObjectLayout> locals;
    /// The compound literals of the body; they get storage at function entry too, so that each evaluation of a
    /// literal in a loop reuses its object (C17 6.5.2.5p16).
    std::unordered_map<const ast::CompoundLiteralExpr*, std::uint32_t> compoundLiteralIds;
    std::vector<const ast::CompoundLiteralExpr*> compoundLiterals;
    std::vector<ObjectLayout> compoundLiteralLayouts;
};

/// What a call of a function does.
enum class FunctionKind : std::uint8_t {
    Defined,     // runs `code`
    Library,     // calls `library`
    Builtin,     // one of the builtins the evaluator implements itself, see `builtin`
    Nondet,      // __VERIFIER_nondet_<type>(): asks the oracle
    Unknown,     // no definition, not in the library: asks the oracle for the return value
    WeakMissing, // a weak function without a definition: its address is 0
    Alias,       // GNU alias("target"): everything goes to the function `aliasTarget`
};

/// The builtins that need the evaluator, because they depend on argument types, lvalues or the frame.
enum class Builtin : std::uint8_t {
    None,
    Assume,                       // __VERIFIER_assume
    NondetMemory,                 // __VERIFIER_nondet_memory(pointer, size)
    VaStart,                      // __builtin_va_start
    VaEnd,                        // __builtin_va_end
    VaCopy,                       // __builtin_va_copy
    ConstantP,                    // __builtin_constant_p
    ObjectSize,                   // __builtin_object_size, __builtin_dynamic_object_size
    PassThrough,                  // __builtin_expect and friends: the first argument, converted to the result type
    Alloca,                       // alloca, __builtin_alloca, __builtin_alloca_with_align
    Overflow,                     // __builtin_{add,sub,mul}_overflow(a, b, &result)
    OverflowP,                    // __builtin_{add,sub,mul}_overflow_p(a, b, c): only the type of c matters
    AtomicLoad,                   // __atomic_load_n(p, order)
    AtomicStore,                  // __atomic_store_n(p, value, order)
    AtomicExchange,               // __atomic_exchange_n(p, value, order), __sync_lock_test_and_set(p, value)
    AtomicCompareExchange,        // __atomic_compare_exchange_n(p, expected pointer, desired, weak, order, order)
    AtomicLoadGeneric,            // __atomic_load(p, result pointer, order)
    AtomicStoreGeneric,           // __atomic_store(p, value pointer, order)
    AtomicExchangeGeneric,        // __atomic_exchange(p, value pointer, result pointer, order)
    AtomicCompareExchangeGeneric, // __atomic_compare_exchange(p, expected pointer, desired pointer, ...)
    AtomicFetchOp,                // __atomic_fetch_<op>, __sync_fetch_and_<op>: the old value
    AtomicOpFetch,                // __atomic_<op>_fetch, __sync_<op>_and_fetch: the new value
    SyncBoolCompareSwap,          // __sync_bool_compare_and_swap(p, old, new)
    SyncValCompareSwap,           // __sync_val_compare_and_swap(p, old, new)
    AtomicTestAndSet,             // __atomic_test_and_set(p, order)
    AtomicClear,                  // __atomic_clear(p, order): a zero byte
    SyncLockRelease,              // __sync_lock_release(p): a zero of the object's type
    AtomicNoOp,                   // fences, __sync_synchronize
    AtomicLockFree,               // __atomic_always_lock_free, __atomic_is_lock_free
};

/// The arithmetic of the overflow and atomic builtins.
enum class BuiltinOperation : std::uint8_t { Add, Sub, Mul, And, Or, Xor, Nand };

struct FunctionEntity {
    Symbol name;
    /// The definition, or else the last declaration seen; the callee of call events.
    const ast::FunctionDecl* decl = nullptr;
    FunctionKind kind = FunctionKind::Unknown;
    const FunctionCode* code = nullptr; // Defined
    LibraryFunction library = nullptr;  // Library; for a Builtin, the library's version, if any
    Builtin builtin = Builtin::None;    // Builtin
    BuiltinOperation operation = BuiltinOperation::Add;
    FunctionId aliasTarget = 0; // Alias
};

/// An object with static storage duration: a global, a static local, or a library object the program declares.
struct GlobalEntity {
    enum class Kind : std::uint8_t {
        Defined,       // storage of the program, initialized from `initialized`
        LibraryObject, // only declared; the library provides it (stdout, errno, ...)
        Alias,         // GNU alias("target"): the storage of the global `aliasTarget`
    };

    Symbol name;
    Kind kind = Kind::Defined;
    const ast::VarDecl* decl = nullptr;        // the last declaration, which has the complete type
    const ast::VarDecl* initialized = nullptr; // the declaration with the initializer, if any
    GlobalId aliasTarget = 0;
    ObjectLayout layout{}; // Defined; larger than sizeof for a flexible array member with an initializer (GNU)
};

class Program {
public:
    Program(ast::AstContext& context, const ast::TranslationUnit& unit)
        : context(context), unit(unit), types(context.target()) {}

    ast::AstContext& context;
    const ast::TranslationUnit& unit;
    TypeTable types;

    std::vector<FunctionEntity> functions;
    std::unordered_map<const ast::FunctionDecl*, FunctionId> functionIds;
    std::unordered_map<Symbol, FunctionId> functionsByName;
    FunctionId main = 0;

    /// In declaration order, which is the order in which a run allocates and initializes them.
    std::vector<GlobalEntity> globals;
    std::unordered_map<const ast::VarDecl*, GlobalId> globalIds;
    std::unordered_map<Symbol, GlobalId> globalsByName; // the globals with linkage

    /// The string literals that need storage (not those that initialize arrays), and the compound literals at file
    /// scope, which have static storage.
    std::vector<const ast::StringLiteralExpr*> strings;
    std::unordered_map<const ast::StringLiteralExpr*, std::uint32_t> stringIds;
    std::vector<const ast::CompoundLiteralExpr*> fileScopeLiterals;
    std::unordered_map<const ast::CompoundLiteralExpr*, std::uint32_t> fileScopeLiteralIds;
    std::vector<ObjectLayout> fileScopeLiteralLayouts;

    std::vector<std::unique_ptr<FunctionCode>> bodies;

    /// The function a declaration refers to; declarations the lowering did not see (in witness constraints) are
    /// found by name. Null if there is none.
    [[nodiscard]] const FunctionEntity* findFunction(const ast::FunctionDecl* decl) const;
    /// The same for objects with static storage duration.
    [[nodiscard]] std::optional<GlobalId> findGlobal(const ast::VarDecl* decl) const;
};

/// Whether an object with static storage duration is identified by its name, because it has linkage (C17 6.2.2).
/// Block-scope statics have none: two of them with the same name are different objects.
[[nodiscard]] inline bool hasLinkage(const ast::VarDecl* decl) {
    return decl->isFileScope() || decl->storage() == ast::StorageClass::Extern;
}

} // namespace nitwit::interp
