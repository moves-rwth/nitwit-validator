#pragma once

// Internal to the interpreter: the state of one run and the evaluator. Execution's member functions are defined by
// area: execution.cpp (start-up, ending, limits), statements.cpp (the instruction loop), calls.cpp (frames, calls,
// arguments, library and oracle), expressions.cpp (operators), lvalues.cpp (objects, loads and stores),
// conversions.cpp (casts), initialization.cpp (initializers, string literals), builtins.cpp (builtins the evaluator
// implements), events.cpp (observer events and the evaluation context) and terms.cpp (terms of values).

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "ast/stmt.hpp"
#include "interp/host_stack.hpp"
#include "interp/interpreter.hpp"
#include "interp/library.hpp"
#include "interp/memory.hpp"
#include "interp/observer.hpp"
#include "interp/oracle.hpp"
#include "interp/program.hpp"
#include "interp/type_info.hpp"
#include "value/arithmetic.hpp"
#include "value/term.hpp"
#include "value/value.hpp"

namespace nitwit::interp {

/// Ends a run. Thrown where the run ends (exit, abort, undefined behaviour, a limit, ...) and caught in
/// Execution::run, so that nested calls and expressions unwind on the host stack.
struct RunEnd {
    Termination termination = Termination::Returned;
    int exitStatus = 0;
    std::string message;
    SourceLocation location;
};

/// An object designated by an lvalue: its address and, for a bit-field, the bits within the bytes at the address. It
/// takes 16 bytes, so that it is returned in registers: the evaluator makes one for every access of an object.
struct LValue {
    Address address = 0;
    std::uint16_t bitWidth = 0; // 0: not a bit-field
    std::uint8_t bitOffset = 0;
    bool hasAddressTerm = false;
    value::TermId addressTermId{}; // if hasAddressTerm

    [[nodiscard]] bool isBitField() const { return bitWidth != 0; }
    /// The term of the address, if it depends on inputs (`*p`, `a[i]`): the term of a pointer to the object
    /// (interp/README.md, "Terms").
    [[nodiscard]] std::optional<value::TermId> addressTerm() const {
        return hasAddressTerm ? std::optional(addressTermId) : std::nullopt;
    }
    /// The object at `address` whose address has the term `term`.
    [[nodiscard]] static LValue at(Address address, std::optional<value::TermId> term) {
        return {.address = address,
                .bitWidth = 0,
                .bitOffset = 0,
                .hasAddressTerm = term.has_value(),
                .addressTermId = term.value_or(value::TermId{})};
    }
};

/// The arguments of a call: their values and the types of the argument expressions (after the conversions sema
/// inserted), which a callee needs to convert them to its parameter types or to fetch them with va_arg.
struct Arguments {
    std::vector<value::Value> values;
    std::vector<ast::QualType> types;
    /// For struct and union arguments, a temporary copy of the argument object made when the argument was evaluated,
    /// with the initialization state and terms of its bytes; 0 for the others.
    std::vector<Address> objects;
};

/// The result of a function of the program: the value, and for a struct or union a temporary object that holds it
/// with the initialization state and terms of its bytes (0 for other types).
struct Returned {
    std::optional<value::Value> value;
    Address object = 0;
};

/// A variable length array or an alloca block: stack objects allocated while a function runs.
struct DynamicObject {
    Address address = 0;
    bool isArray = false;
};

/// The number of elements of a variable length array type, fixed where its declaration was reached (C17 6.7.6.2p5).
struct ArraySize {
    const ast::ArrayType* array = nullptr;
    std::uint64_t count = 0;
    bool dependsOnInputs = false; // whether the size expression's value has a term
};

struct Frame {
    const FunctionEntity* function = nullptr;
    const FunctionCode* body = nullptr;
    std::vector<Address> locals;        // by VarDecl::localIndex; 0 for variable length arrays before their declaration
    std::vector<Address> literals;      // compound literals, by FunctionCode::compoundLiteralIds
    std::vector<DynamicObject> dynamic; // in allocation order, so they end in LIFO order
    std::uint32_t arrayCount = 0;       // the variable length arrays in `dynamic`
    /// The sizes of the variable length array types evaluated in this frame, newest last.
    std::vector<ArraySize> arraySizes;
    Arguments variadic; // the arguments after the named parameters
    /// Heap objects holding struct and union rvalues: arguments, results, and values whose members are addressed, as
    /// in `f().a[1]` (C17 6.2.4p8). They end with the full expression, i.e. after the instruction that made them.
    std::vector<Address> temporaries;
    std::uint64_t serial = 0; // identifies the frame in va_list handles
    /// The number of va_list cursors when the frame was entered; those the frame made end with it.
    std::size_t vaCursorBase = 0;
    Returned result;
    value::Value statementValue; // the value of the statement expression that ended last
};

/// The state of a va_list: the frame whose variadic arguments it walks, and the next one.
struct VaCursor {
    std::uint64_t frameSerial = 0;
    std::size_t depth = 0; // the frame's index in the call stack
    std::size_t next = 0;
};

class Execution final : public EvaluationContext, public LibraryContext {
public:
    Execution(std::shared_ptr<const Program> program, InputOracle& oracle, ExecutionObserver& observer,
              RunOptions options);
    ~Execution() override;
    Execution(const Execution&) = delete;
    Execution& operator=(const Execution&) = delete;

    RunResult run();

    // EvaluationContext
    std::expected<value::Value, std::string> evaluate(const ast::Expr* expr) override;
    [[nodiscard]] const ast::FunctionDecl* function() const override;
    [[nodiscard]] const value::TermTable& terms() const override { return terms_; }

    // LibraryContext
    [[nodiscard]] Memory& memory() override { return memory_; }
    [[nodiscard]] const TargetInfo& target() const override { return types_.target(); }
    [[nodiscard]] std::ostream* output() override { return options_.output; }
    [[nodiscard]] std::expected<Address, MemoryError> allocateHeap(std::uint64_t size, bool zeroed) override;
    [[nodiscard]] Address objectAddress(std::string_view name) override;

private:
    // ---- execution.cpp: start-up, ending and limits ----
    void allocateStaticStorage();
    void initializeStaticStorage();
    value::Value callMain();
    [[noreturn]] void stop(Termination termination, std::string message, SourceLocation location) const;
    /// Undefined behaviour without a defined result: ends the run (interp/README.md, "Undefined behaviour").
    [[noreturn]] void undefined(std::string message, const ast::Expr* where) const;
    [[noreturn]] void memoryError(const MemoryError& error, bool isWrite, const ast::Expr* where) const;
    void countStep();
    /// Ends the run with a limit if the host stack is nearly exhausted (see host_stack.hpp).
    void checkHostStack(const ast::Expr* where) const;
    /// Allocates and accounts for the memory limit; running out of memory or address space ends the run.
    Address allocate(Region region, std::uint64_t size, std::uint64_t alignment, std::string_view name,
                     const ast::Expr* where);

    // ---- statements.cpp ----
    /// Runs the instructions of `frame` from `begin` until the function returns or, for a statement expression's
    /// range, until its end.
    void execute(Frame& frame, std::uint32_t begin, const InstructionRange* range);
    void declare(Frame& frame, const ast::Decl* decl);
    void evaluateArraySizes(Frame& frame, ast::QualType type);
    /// Ends the dynamic stack objects above `arrayDepth` variable length arrays.
    void releaseDynamic(Frame& frame, std::uint32_t arrayDepth);
    std::uint32_t switchTarget(const Frame& frame, const Instruction& instruction);
    Returned returnedValue(const ast::Expr* expr);

    // ---- calls.cpp ----
    /// Calls a function. For a struct or union result, `object` (if not null) receives the temporary object that holds
    /// it, or 0 if there is none (library functions).
    value::Value call(const ast::CallExpr* call, bool isValueUsed, Address* object = nullptr);
    /// The called function; null for a pointer to no function.
    const FunctionEntity* resolveCallee(const ast::CallExpr* call);
    /// The result of a call in the representation of the call's type; empty for void. `from` is the type of the
    /// result, if the callee is a function of the program.
    std::optional<value::Value> inCallType(value::Value result, std::string_view callee, const TypeInfo* from,
                                           const TypeInfo& type, const ast::Expr* where);
    void evaluateArguments(const ast::CallExpr* call, Arguments& arguments);
    /// Runs a function of the program; the result has no value if the function ended without returning one.
    Returned invoke(const FunctionEntity& function, Arguments& arguments, const ast::CallExpr* site);
    void enterFrame(Frame& frame, const FunctionEntity& function, Arguments& arguments, const ast::CallExpr* site);
    void checkCallWithoutPrototype(const ast::FunctionType& definition, const Arguments& arguments,
                                   const ast::CallExpr* site);
    void leaveFrame(Frame& frame);
    std::optional<value::Value> callLibrary(const FunctionEntity& function, Arguments& arguments,
                                            const ast::CallExpr* site);
    /// The value a library function returned, or the end of the run it asks for.
    std::optional<value::Value> libraryResult(const FunctionEntity& function, LibraryResult result,
                                              const ast::CallExpr* site);
    /// A value from the oracle, with its input term; also counts the request.
    value::Value requestInput(InputRequest::Kind kind, const ast::Expr* site, ast::QualType type,
                              const ast::FunctionDecl* function);
    /// The value a function without definition returns: from the oracle for scalar types, zero for others.
    std::optional<value::Value> unknownResult(InputRequest::Kind kind, const ast::CallExpr* site,
                                              const FunctionEntity& function);
    /// The function at a code address; null if there is none.
    [[nodiscard]] const FunctionEntity* functionAt(Address address) const;
    [[nodiscard]] const FunctionEntity& entityOf(const ast::FunctionDecl* decl) const;
    [[nodiscard]] const FunctionEntity& followAlias(const FunctionEntity& function) const;

    // ---- expressions.cpp ----
    /// The value of an rvalue expression.
    value::Value rvalue(const ast::Expr* expr);
    /// Evaluates an expression whose value is not used (C17 6.8.3p2).
    void effect(const ast::Expr* expr);
    /// The value of a controlling expression or of an operand of `!`, `&&` or `||`, which must be a scalar; it is
    /// tested for comparing unequal to 0 (C17 6.8.4.1p2, 6.5.13p3, ...).
    value::Value condition(const ast::Expr* expr);
    value::Value unary(const ast::UnaryExpr* expr);
    /// A pointer to the object, with the term of its address.
    value::Value pointerTo(const LValue& object);
    value::Value negation(const ast::UnaryExpr* expr);
    value::Value complement(const ast::UnaryExpr* expr);
    value::Value logicalNot(const ast::UnaryExpr* expr);
    value::Value binary(const ast::BinaryExpr* expr);
    value::Value arithmetic(ast::BinaryOperator op, const value::Value& lhs, const value::Value& rhs,
                            ast::QualType lhsType, ast::QualType rhsType, ast::QualType resultType,
                            const ast::Expr* where);
    // The operations of arithmetic.hpp with their terms; checked() reports their undefined behaviour.
    value::Value integerOperation(value::IntegerOperator op, const value::Value& lhs, const value::Value& rhs,
                                  value::IntegerFormat format, const ast::Expr* where);
    value::Value floatOperation(value::FloatOperator op, const value::Value& lhs, const value::Value& rhs,
                                value::FloatFormat format, const ast::Expr* where);
    value::Value shiftOperation(value::ShiftOperator op, const value::Value& lhs, value::IntegerFormat lhsFormat,
                                const value::Value& rhs, value::IntegerFormat rhsFormat, const ast::Expr* where);
    value::Value pointerOffset(const value::Value& pointer, const value::Value& index, ast::QualType pointerType,
                               bool subtract, const ast::Expr* where);
    value::Value pointerDifference(const value::Value& lhs, const value::Value& rhs, ast::QualType pointerType,
                                   ast::QualType resultType, const ast::Expr* where);
    value::Value comparison(ast::BinaryOperator op, const ast::BinaryExpr* expr);
    value::Value logical(const ast::BinaryExpr* expr);
    value::Value assignment(const ast::BinaryExpr* expr, bool isValueUsed);
    [[nodiscard]] bool isLeftOperandFirst(const ast::BinaryExpr* expr) const;
    /// A struct or union assignment; returns the assigned object.
    Address assignRecord(const ast::BinaryExpr* expr);
    value::Value compoundAssignment(const ast::BinaryExpr* expr);
    value::Value increment(const ast::UnaryExpr* expr);
    value::Value conditional(const ast::ConditionalExpr* expr, bool isValueUsed);
    /// Evaluates the condition of `?:` and reports it; returns the selected operand, or null if GNU `c ?: b` yields
    /// the condition's value, which is then in `condition`.
    const ast::Expr* selectOperand(const ast::ConditionalExpr* expr, value::Value& condition);
    value::Value statementExpression(const ast::StatementExpr* expr);
    value::Value sizeOrAlignment(const ast::SizeOfAlignOfExpr* expr);
    value::Value offsetOf(const ast::OffsetOfExpr* expr);
    /// An integer constant expression of sema, evaluated once per run.
    std::optional<value::Value> constant(const ast::Expr* expr);
    value::Value literal(const ast::Expr* expr);
    /// Reports an operation's undefined behaviour if it continues with the hardware result (signed overflow, shifts,
    /// out-of-range floating conversions) and ends the run for division by zero.
    value::Value checked(value::Result result, const ast::Expr* where);

    // ---- lvalues.cpp ----
    LValue lvalue(const ast::Expr* expr);
    Address variableAddress(const ast::VarDecl* decl, const ast::Expr* where);
    Address functionAddress(const ast::FunctionDecl* decl);
    LValue member(const ast::MemberExpr* expr);
    /// An object that holds the value of a struct or union rvalue expression, with the initialization state and
    /// terms of its bytes: the object read for an lvalue, the result object of a call, or a new temporary.
    Address recordObject(const ast::Expr* expr);
    /// A heap object that lives until the instruction of `owner` (the current frame if null) has finished.
    Address temporary(std::uint64_t size, Frame* owner, const ast::Expr* where);
    void releaseTemporaries(Frame& frame);
    /// Reads a scalar or the bytes of a record. A read of uninitialized bytes asks the oracle (UninitializedRead) and
    /// stores the answer, so that later reads agree. `site` is the expression that designates the object.
    value::Value load(const LValue& object, ast::QualType type, const ast::Expr* site);
    /// The scalar of `type` in `bytes`, for which memory knows `term`, with its term.
    value::Value decodeLoaded(std::span<const std::byte> bytes, const TypeInfo& type, const RangeTerm& term,
                              const ast::Expr* site);
    /// A read of a scalar with uninitialized bytes: an input, stored at `address`. `bytes` are the current bytes.
    value::Value loadUninitialized(Address address, ast::QualType type, const TypeInfo& info,
                                   std::span<const std::byte> bytes, const ast::Expr* site);
    /// Attaches the term of `result`, a scalar of `type` read from `bytes`, for which memory knows `term`.
    void attachLoadedTerm(value::Value& result, const RangeTerm& term, std::span<const std::byte> bytes,
                          const TypeInfo& type, const ast::Expr* site);
    value::Value loadBitField(const LValue& object, const TypeInfo& type, const ast::Expr* site);
    /// Writes a value of `type`; returns the value the object then holds (truncated for bit-fields).
    value::Value store(const LValue& object, ast::QualType type, const value::Value& stored, const ast::Expr* where);
    /// Writes the value of a struct or union rvalue expression to `destination`, with the initialization state and
    /// terms of its bytes.
    void storeRecord(Address destination, std::uint64_t size, const ast::Expr* source);
    /// The bytes of a struct or union object as a value. Its term is opaque if a byte depends on inputs, so that
    /// writeRecord keeps that dependency.
    value::Value loadRecord(Address address, std::uint64_t size, const ast::Expr* where);
    /// Writes the bytes of a struct or union value; they are opaque if the value has a term.
    void writeRecord(Address address, std::span<const std::byte> bytes, std::optional<value::TermId> term,
                     const ast::Expr* where);
    /// Memory::copy with the failing side reported.
    void copyObject(Address destination, Address source, std::uint64_t size, const ast::Expr* where);
    std::vector<std::byte> readBytes(Address address, std::uint64_t size, const ast::Expr* where);
    /// Memory::write, with the term of the scalar the bytes represent if `term` is not null.
    void writeBytes(Address address, std::span<const std::byte> bytes, const ast::Expr* where,
                    const StoredTerm* term = nullptr);
    /// The size of a type, also of a variable length array type in the current frame. `dependsOnInputs`, if given, is
    /// set if the size of a variable length array in the type depends on inputs.
    std::uint64_t sizeOf(ast::QualType type, const ast::Expr* where, bool* dependsOnInputs = nullptr);
    std::uint64_t arraySize(const ast::ArrayType* array, bool* dependsOnInputs = nullptr);
    [[nodiscard]] SourceLocation locationOf(const ast::Expr* where) const {
        return where != nullptr ? where->location() : location_;
    }

    // ---- conversions.cpp ----
    value::Value castValue(ast::CastKind kind, const ast::Expr* operand, ast::QualType to, const ast::Expr* where);
    /// Converts a scalar value between types as assignment does (C17 6.3.1), with the term of the conversion.
    value::Value convert(const value::Value& value, const TypeInfo& from, const TypeInfo& to, const ast::Expr* where);
    value::Value convert(const value::Value& value, ast::QualType from, ast::QualType to, const ast::Expr* where);
    /// value::convertInteger with its term; `value` itself if the formats are equal.
    value::Value convertInteger(const value::Value& value, value::IntegerFormat from, value::IntegerFormat to,
                                const ast::Expr* where);
    /// value::toBool with its term.
    value::Value toBool(const value::Value& value, value::ScalarFormat from, const ast::Expr* where);
    /// A conversion from or to a floating type, between different types, with its term.
    value::Value convertFloating(const value::Value& value, const TypeInfo& from, const TypeInfo& to,
                                 const ast::Expr* where);
    value::Value zeroOf(const TypeInfo& type) const;

    // ---- initialization.cpp ----
    /// Initializes an object (C17 6.7.9). `isZeroed`: the object already holds zeros (static storage), so implicit
    /// zeros need not be written.
    void initialize(Address address, ast::QualType type, const ast::Expr* initializer, bool isZeroed);
    void initializeArray(Address address, ast::QualType type, const ast::Expr* initializer, bool isZeroed);
    void initializeRecord(Address address, ast::QualType type, const ast::InitListExpr* list, bool isZeroed);
    void initializeFromString(Address address, std::uint64_t size, const ast::StringLiteralExpr* string);
    Address stringAddress(const ast::StringLiteralExpr* string);
    std::vector<std::byte> stringBytes(const ast::StringLiteralExpr* string, std::uint64_t size) const;
    LValue compoundLiteral(const ast::CompoundLiteralExpr* literal);

    // ---- builtins.cpp ----
    /// The builtins whose arguments are lvalues, unevaluated, or only types (va_start, __builtin_constant_p, ...):
    /// they get the argument expressions and evaluate them themselves.
    std::optional<value::Value> builtinOnExpressions(const FunctionEntity& function, const ast::CallExpr* call);
    /// The other builtins of the evaluator, with the evaluated arguments.
    std::optional<value::Value> builtinOnValues(const FunctionEntity& function, const ast::CallExpr* call,
                                                Arguments& arguments);
    /// The address of the va_list object an argument designates (or points to, as CIL writes it).
    Address vaListAddress(const ast::Expr* list);
    /// The cursor a va_list object's bytes refer to; undefined if there is none or its frame has returned.
    VaCursor& vaCursor(std::span<const std::byte> handle, const ast::Expr* where);
    void writeVaHandle(Address list, std::size_t cursor, const ast::Expr* where);
    value::Value vaArg(const ast::VaArgExpr* expr);
    /// Replaces va_list arguments of a library call by the arguments they refer to (library.hpp).
    void expandVaLists(Arguments& arguments, const ast::CallExpr* call);
    value::Value overflow(const FunctionEntity& function, const ast::CallExpr* call, const Arguments& arguments);
    std::optional<value::Value> atomic(const FunctionEntity& function, const ast::CallExpr* call,
                                       const Arguments& arguments);
    void nondetMemory(const FunctionEntity& function, const ast::CallExpr* call, const Arguments& arguments);
    value::Value allocateOnStack(const ast::CallExpr* call, const Arguments& arguments);

    // ---- events.cpp ----
    void obey(Directive directive, SourceLocation location) const;
    void beforeStatement(const ast::Stmt* stmt);
    void afterCondition(const ConditionEvent& event);
    void reportUndefined(value::UndefinedBehavior kind, const ast::Expr* where);

    // ---- terms.cpp ----
    /// Whether the run records terms (not in replay mode, RunOptions::terms).
    [[nodiscard]] bool recordsTerms() const { return options_.terms.maxTerms > 0; }
    /// Attaches `term`, which the evaluator recorded for `value` or read from memory; with RunOptions::checkTerms, the
    /// run ends if the term does not evaluate to the value. In place, so that values are not moved. The frequent
    /// operations call it (and the TermTable) only if an operand has a term, so that operations on values without
    /// terms, as in programs without inputs, do nothing else: recording unconditionally, with only the checks of the
    /// TermTable, cost 5 % of the run time.
    void attach(value::Value& value, std::optional<value::TermId> term, const ast::Expr* where) {
        value.setTerm(term);
        if (options_.checkTerms && term) {
            checkTerm(value, where);
        }
    }
    void checkTerm(const value::Value& value, const ast::Expr* where);
    /// The term of a value computed by an operation without a term kind: opaque if the value depends on inputs.
    [[nodiscard]] std::optional<value::TermId> opaqueIf(bool dependsOnInputs) const {
        return dependsOnInputs ? terms_.opaque() : std::nullopt;
    }
    /// Memory::markOpaque, if the run records terms.
    void markOpaque(Address address, std::uint64_t size);

    std::shared_ptr<const Program> program_;
    const Program& code_;
    ast::AstContext& context_;
    const TypeTable& types_;
    InputOracle& oracle_;
    ExecutionObserver& observer_;
    RunOptions options_;
    /// The library objects by name; the keys name their allocations, so the map outlives memory_.
    std::unordered_map<std::string, Address> libraryObjects_;
    Memory memory_;
    value::TermTable terms_;

    std::vector<Address> functionAddresses_; // by FunctionId; 0 for weak functions without definition
    std::unordered_map<Address, FunctionId> functionsAt_;
    std::vector<Address> globalAddresses_;   // by GlobalId
    std::vector<Address> stringAddresses_;   // by Program::stringIds
    std::vector<Address> fileScopeLiterals_; // by Program::fileScopeLiteralIds

    std::vector<Frame*> frames_; // the call stack; the current frame is the last
    Frame* frame_ = nullptr;     // null while static storage is initialized
    std::uint64_t nextFrameSerial_ = 1;
    std::vector<VaCursor> vaCursors_;

    std::uint64_t steps_ = 0;
    std::uint32_t inputs_ = 0;
    std::unordered_map<const ast::Expr*, std::uint32_t> occurrences_; // input requests per site
    std::unordered_map<const ast::Expr*, Int128> constants_;          // sizeof and friends, evaluated once per run

    /// While the observer evaluates a constraint: undefined behaviour and side effects are errors.
    bool isEvaluatingConstraint_ = false;
    /// Whether evaluations are steps of the program that the observer sees: not while static storage is initialized
    /// or a constraint is evaluated.
    bool isReporting_ = false;
    std::optional<HostStack> hostStack_;        // of the thread that runs
    const value::Value* returnValue_ = nullptr; // \result during a ReturnEvent
    SourceLocation location_;                   // of the statement being executed, for messages
    /// The end of the run for a term that does not give its value (RunOptions::checkTerms), kept for a mismatch found
    /// while the observer evaluates a constraint.
    std::optional<RunEnd> termMismatch_;
};

/// What the undefined behaviour is, for messages (events.cpp).
[[nodiscard]] std::string_view describe(value::UndefinedBehavior kind);

/// Whether a scalar compares unequal to 0, which is what conditions and the operands of `!`, `&&` and `||` test
/// (C17 6.8.4.1p2, 6.5.3.3p5, 6.5.13p3, ...).
[[nodiscard]] inline bool isNonZero(const value::Value& scalar) {
    return scalar.kind() == value::Value::Kind::Integer ? scalar.bits() != 0 : value::toBool(scalar).bits() != 0;
}

/// For RunOptions::checkTerms: what is wrong with `term` as the term of `value`, which the run computed; nullopt if it
/// evaluates to exactly that value with the run's inputs, or is the opaque term.
[[nodiscard]] std::optional<std::string> termMismatch(const value::TermTable& terms, value::TermId term,
                                                      const value::Value& value);

} // namespace nitwit::interp
