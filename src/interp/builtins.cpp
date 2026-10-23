// The builtins the evaluator implements itself, because they depend on the types of their arguments, on lvalues or on
// the frame: variadic arguments, __VERIFIER_assume and __VERIFIER_nondet_memory, alloca, __builtin_constant_p, the
// type-generic overflow builtins and the atomic builtins.

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

#include "ast/casting.hpp"
#include "interp/execution.hpp"
#include "sema/constant.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

/// An integer of unbounded precision, as far as the overflow builtins need it: a sign and a magnitude below 2^128.
/// `isHuge` marks results whose magnitude does not fit; their low 128 bits are still right.
struct ExactInteger {
    bool isNegative = false;
    UInt128 magnitude = 0;
    bool isHuge = false;

    static ExactInteger of(const value::Value& value, value::IntegerFormat format) {
        if (format.isSigned && value.signedBits() < 0) {
            return {.isNegative = true, .magnitude = UInt128{0} - value.bits(), .isHuge = false};
        }
        return {.isNegative = false, .magnitude = value.bits(), .isHuge = false};
    }

    [[nodiscard]] ExactInteger negated() const {
        return {.isNegative = !isNegative && magnitude != 0, .magnitude = magnitude, .isHuge = isHuge};
    }

    /// The low 128 bits of the two's complement representation.
    [[nodiscard]] UInt128 bits() const { return isNegative ? UInt128{0} - magnitude : magnitude; }

    /// Whether the value is representable in `format`.
    [[nodiscard]] bool fits(value::IntegerFormat format) const {
        if (isHuge) {
            return false;
        }
        if (!format.isSigned) {
            return !isNegative && (format.bits == 128 || magnitude >> format.bits == 0);
        }
        const UInt128 limit = UInt128{1} << (format.bits - 1);
        return isNegative ? magnitude <= limit : magnitude < limit;
    }
};

ExactInteger add(const ExactInteger& a, const ExactInteger& b) {
    ExactInteger result;
    result.isHuge = a.isHuge || b.isHuge;
    if (a.isNegative == b.isNegative) {
        result.isNegative = a.isNegative;
        result.magnitude = a.magnitude + b.magnitude;
        result.isHuge = result.isHuge || result.magnitude < a.magnitude;
    } else if (a.magnitude >= b.magnitude) {
        result.isNegative = a.isNegative && a.magnitude != b.magnitude;
        result.magnitude = a.magnitude - b.magnitude;
    } else {
        result.isNegative = b.isNegative;
        result.magnitude = b.magnitude - a.magnitude;
    }
    return result;
}

ExactInteger multiply(const ExactInteger& a, const ExactInteger& b) {
    ExactInteger result;
    result.magnitude = a.magnitude * b.magnitude;
    result.isNegative = (a.isNegative != b.isNegative) && result.magnitude != 0;
    result.isHuge = a.isHuge || b.isHuge || (a.magnitude != 0 && result.magnitude / a.magnitude != b.magnitude);
    return result;
}

/// The type of an argument as written, before the default argument promotions of an implicitly declared builtin.
ast::QualType writtenType(const ast::Expr* argument) {
    const ast::Expr* current = ast::ignoreParens(argument);
    while (const auto* castExpr = ast::dyn_cast<ast::ImplicitCastExpr>(current)) {
        if (castExpr->castKind() != ast::CastKind::IntegralCast && castExpr->castKind() != ast::CastKind::FloatingCast
            && castExpr->castKind() != ast::CastKind::LValueToRValue) {
            break;
        }
        current = ast::ignoreParens(castExpr->operand());
    }
    return current->type();
}

} // namespace

// ---- Builtins with special arguments ----

std::optional<value::Value> Execution::builtinOnExpressions(const FunctionEntity& function, const ast::CallExpr* call) {
    const std::span<ast::Expr* const> arguments = call->arguments();
    switch (function.builtin) {
    case Builtin::VaStart: {
        // C17 7.16.1.4: the list walks the variadic arguments of the current call.
        NITWIT_ASSERT(frame_ != nullptr, "va_start outside a function");
        const Address list = vaListAddress(arguments[0]);
        vaCursors_.push_back({.frameSerial = frame_->serial, .depth = frames_.size() - 1, .next = 0});
        writeVaHandle(list, vaCursors_.size() - 1, call);
        return std::nullopt;
    }
    case Builtin::VaEnd:
        (void)vaListAddress(arguments[0]);
        return std::nullopt;
    case Builtin::VaCopy: {
        const Address destination = vaListAddress(arguments[0]);
        const value::Value source = rvalue(arguments[1]);
        const VaCursor copy = vaCursor(source.asBytes(), call);
        vaCursors_.push_back(copy);
        writeVaHandle(destination, vaCursors_.size() - 1, call);
        return std::nullopt;
    }
    case Builtin::ConstantP: {
        // The operand is not evaluated; GCC at -O0 folds exactly the integer constant expressions (and literals).
        const ast::Expr* operand = arguments.empty() ? nullptr : ast::ignoreParens(arguments[0]);
        bool isConstant = false;
        if (operand != nullptr) {
            isConstant = ast::isa<ast::FloatingLiteralExpr>(operand) || ast::isa<ast::StringLiteralExpr>(operand)
                         || (types_.describe(operand->type()).isIntegral()
                             && sema::evaluateIntegerConstant(context_, operand).has_value());
        }
        return value::Value::integer(isConstant ? 1 : 0, types_.describe(call->type()).integer);
    }
    case Builtin::OverflowP: {
        // The third argument only gives the type; it is not evaluated (GCC, "Integer Overflow Builtins").
        if (arguments.size() != 3) {
            undefined(std::format("'{}' needs three arguments", function.name.text()), call);
        }
        Arguments values;
        values.values.resize(2);
        values.types.resize(2);
        for (std::size_t i = 2; i-- > 0;) {
            values.values[i] = rvalue(arguments[i]);
            values.types[i] = arguments[i]->type();
        }
        values.types.push_back(writtenType(arguments[2]));
        return overflow(function, call, values);
    }
    default:
        break;
    }
    NITWIT_UNREACHABLE("not a builtin with special arguments");
}

std::optional<value::Value> Execution::builtinOnValues(const FunctionEntity& function, const ast::CallExpr* call,
                                                       Arguments& arguments) {
    switch (function.builtin) {
    case Builtin::Assume:
        // An execution in which the argument is 0 is infeasible; the run ends without a verdict on it.
        if (!arguments.values.empty() && value::toBool(arguments.values[0]).bits() == 0) {
            stop(Termination::AssumeFailed, "__VERIFIER_assume with a false condition", call->location());
        }
        return std::nullopt;
    case Builtin::NondetMemory:
        nondetMemory(function, call, arguments);
        return std::nullopt;
    case Builtin::Alloca:
        return allocateOnStack(call, arguments);
    case Builtin::Overflow:
        return overflow(function, call, arguments);
    case Builtin::PassThrough:
        // The value of the first argument, converted with the term of the conversion.
        if (arguments.values.empty()) {
            undefined(std::format("'{}' needs an argument", function.name.text()), call);
        }
        return convert(arguments.values[0], arguments.types[0], call->type(), call);
    case Builtin::ObjectSize:
        // GCC knows the size of a declared object even at -O0; for other pointers, the library answers.
        if (const std::optional<std::uint64_t> size = sema::knownObjectSize(context_, call)) {
            return value::Value::integer(*size, types_.describe(call->type()).integer);
        }
        NITWIT_ASSERT(function.library != nullptr, "the library has no __builtin_object_size");
        return callLibrary(function, arguments, call);
    case Builtin::None:
    case Builtin::VaStart:
    case Builtin::VaEnd:
    case Builtin::VaCopy:
    case Builtin::ConstantP:
    case Builtin::OverflowP:
        break;
    default:
        return atomic(function, call, arguments);
    }
    NITWIT_UNREACHABLE("a builtin with special arguments");
}

// ---- Variadic arguments ----
//
// A va_list object holds a handle: the number of a VaCursor plus 1, in its first four bytes (the rest is zero). Copying
// the object copies the handle, so a va_list passed to another function walks on in the same cursor, as on x86-64.

Address Execution::vaListAddress(const ast::Expr* list) {
    if (list->isLValue()) {
        return lvalue(list).address;
    }
    // CIL passes the address of the va_list instead (sema/README.md).
    return static_cast<Address>(rvalue(list).bits());
}

void Execution::writeVaHandle(Address list, std::size_t cursor, const ast::Expr* where) {
    const std::uint64_t size = types_.describe(context_.builtin(ast::BuiltinKind::VaList)).size;
    std::vector<std::byte> bytes(size);
    const auto handle = static_cast<std::uint32_t>(cursor + 1);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[i] = static_cast<std::byte>((handle >> (8 * i)) & 0xFF);
    }
    writeBytes(list, bytes, where);
}

VaCursor& Execution::vaCursor(std::span<const std::byte> handle, const ast::Expr* where) {
    std::uint32_t number = 0;
    for (std::size_t i = 0; i < 4 && i < handle.size(); ++i) {
        number |= std::to_integer<std::uint32_t>(handle[i]) << (8 * i);
    }
    if (number == 0 || number > vaCursors_.size()) {
        undefined("a va_list that va_start did not initialize, or whose function has returned", where);
    }
    VaCursor& cursor = vaCursors_[number - 1];
    if (cursor.depth >= frames_.size() || frames_[cursor.depth]->serial != cursor.frameSerial) {
        undefined("a va_list whose function has returned", where);
    }
    return cursor;
}

// C17 7.16.1.1: the next argument, which must have the requested type, up to signedness and void/char pointers.
value::Value Execution::vaArg(const ast::VaArgExpr* expr) {
    if (isEvaluatingConstraint_) {
        stop(Termination::Unsupported, "a constraint must not use va_arg", expr->location());
    }
    const Address list = vaListAddress(expr->list());
    const std::vector<std::byte> handle =
        readBytes(list, types_.describe(context_.builtin(ast::BuiltinKind::VaList)).size, expr);
    VaCursor& cursor = vaCursor(handle, expr);
    const Arguments& variadic = frames_[cursor.depth]->variadic;
    if (cursor.next >= variadic.values.size()) {
        undefined("va_arg after the last argument", expr);
    }
    const value::Value& argument = variadic.values[cursor.next];
    const TypeInfo from = types_.describe(variadic.types[cursor.next]);
    ++cursor.next;
    const TypeInfo to = types_.describe(expr->argumentType());
    // C17 7.16.1.1p2: the types must be compatible, except for signed and unsigned versions of a type (and, as here,
    // integers and pointers of the same width); in particular, the sizes must agree.
    const bool isCompatible = (from.isIntegral() && to.isIntegral() && from.integer.bits == to.integer.bits)
                              || (from.representation == Representation::Floating
                                  && to.representation == Representation::Floating && from.floating == to.floating)
                              || (from.isBytes() && to.isBytes() && from.size == to.size);
    if (!isCompatible) {
        undefined("va_arg with a type that does not match the argument", expr);
    }
    if (to.isIntegral()) {
        return convertInteger(argument, from.integer, to.integer, expr); // only the signedness may differ
    }
    return argument;
}

void Execution::expandVaLists(Arguments& arguments, const ast::CallExpr* call) {
    for (std::size_t i = 0; i < arguments.values.size(); ++i) {
        if (types_.describe(arguments.types[i]).representation != Representation::VaList) {
            continue;
        }
        const VaCursor& cursor = vaCursor(arguments.values[i].asBytes(), call);
        const Arguments& variadic = frames_[cursor.depth]->variadic;
        const auto first = static_cast<std::ptrdiff_t>(cursor.next);
        const auto at = static_cast<std::ptrdiff_t>(i);
        Arguments expanded;
        expanded.values.assign(arguments.values.begin(), arguments.values.begin() + at);
        expanded.values.insert(expanded.values.end(), variadic.values.begin() + first, variadic.values.end());
        expanded.values.insert(expanded.values.end(), arguments.values.begin() + at + 1, arguments.values.end());
        expanded.types.assign(arguments.types.begin(), arguments.types.begin() + at);
        expanded.types.insert(expanded.types.end(), variadic.types.begin() + first, variadic.types.end());
        expanded.types.insert(expanded.types.end(), arguments.types.begin() + at + 1, arguments.types.end());
        arguments = std::move(expanded);
        return; // the library's v-functions take one va_list, as their last argument
    }
}

// ---- __VERIFIER_nondet_memory and alloca ----

// The harness fills the memory byte by byte; every byte is an input of type unsigned char.
void Execution::nondetMemory(const FunctionEntity& function, const ast::CallExpr* call, const Arguments& arguments) {
    if (arguments.values.size() < 2) {
        undefined("__VERIFIER_nondet_memory needs a pointer and a size", call);
    }
    const auto address = static_cast<Address>(arguments.values[0].bits());
    const auto size = static_cast<std::uint64_t>(arguments.values[1].bits());
    const ast::QualType byte = context_.builtin(ast::BuiltinKind::UnsignedChar);
    for (std::uint64_t i = 0; i < size; ++i) {
        const value::Value input = requestInput(InputRequest::Kind::NondetCall, call, byte, function.decl);
        (void)store({.address = address + i}, byte, input, call);
    }
}

// The block lives until the function returns, or until a variable length array allocated before it ends (GCC frees
// both with the stack pointer). Freeing it is undefined: it is not a heap block.
value::Value Execution::allocateOnStack(const ast::CallExpr* call, const Arguments& arguments) {
    NITWIT_ASSERT(frame_ != nullptr, "alloca outside a function");
    const auto size = static_cast<std::uint64_t>(arguments.values.empty() ? 0 : arguments.values[0].bits());
    std::uint64_t alignment = types_.target().maxAlign();
    if (arguments.values.size() >= 2) {
        // __builtin_alloca_with_align(size, alignment in bits)
        alignment = std::max<std::uint64_t>(alignment, static_cast<std::uint64_t>(arguments.values[1].bits()) / 8);
    }
    if (!std::has_single_bit(alignment)) {
        undefined("__builtin_alloca_with_align with an alignment that is not a power of two", call);
    }
    const Address block = allocate(Region::Stack, size, alignment, "alloca", call);
    frame_->dynamic.push_back({.address = block, .isArray = false});
    value::Value result = value::Value::integer(block, types_.pointerFormat());
    // As for malloc, the result of a call with an argument with a term is opaque.
    result.setTerm(opaqueIf(std::ranges::any_of(
        arguments.values, [](const value::Value& argument) { return argument.term().has_value(); })));
    return result;
}

// ---- Overflow builtins (GCC, "Integer Overflow Builtins") ----

// The result is computed with infinite precision, stored wrapped to the type of *result, and the return value tells
// whether it did not fit.
value::Value Execution::overflow(const FunctionEntity& function, const ast::CallExpr* call,
                                 const Arguments& arguments) {
    const bool isPredicate = function.builtin == Builtin::OverflowP;
    if (arguments.types.size() < 3 || arguments.values.size() < (isPredicate ? 2 : 3)) {
        undefined("an overflow builtin needs three arguments", call);
    }
    const value::IntegerFormat leftFormat = types_.describe(arguments.types[0]).integer;
    const value::IntegerFormat rightFormat = types_.describe(arguments.types[1]).integer;
    const ExactInteger a = ExactInteger::of(arguments.values[0], leftFormat);
    const ExactInteger b = ExactInteger::of(arguments.values[1], rightFormat);
    ExactInteger exact;
    switch (function.operation) {
    case BuiltinOperation::Add:
        exact = add(a, b);
        break;
    case BuiltinOperation::Sub:
        exact = add(a, b.negated());
        break;
    case BuiltinOperation::Mul:
        exact = multiply(a, b);
        break;
    default:
        NITWIT_UNREACHABLE("not an overflow operation");
    }
    const ast::QualType resultType = isPredicate ? arguments.types[2] : TypeTable::pointee(arguments.types[2]);
    const TypeInfo result = types_.describe(resultType);
    // Computed with unbounded precision, which no term kind records.
    const std::optional<value::TermId> term =
        opaqueIf(arguments.values[0].term().has_value() || arguments.values[1].term().has_value());
    if (!isPredicate) {
        value::Value wrapped = value::Value::integer(exact.bits(), result.integer);
        wrapped.setTerm(term);
        (void)store({.address = static_cast<Address>(arguments.values[2].bits())}, resultType, wrapped, call);
    }
    const bool overflowed = !exact.fits(result.integer);
    value::Value flag = value::Value::integer(overflowed ? 1 : 0, types_.describe(call->type()).integer);
    flag.setTerm(term);
    return flag;
}

// ---- Atomic builtins (GCC, "Built-in Functions for Memory Model Aware Atomic Operations" and "Legacy __sync
// Built-in Functions") ----
//
// A run has one thread, so they are plain loads, stores and read-modify-writes; the memory orders do not matter. Sema
// declares most of them implicitly as `int ()`, so the type of the object comes from the pointer argument, and the
// result is converted to the type of the call.

std::optional<value::Value> Execution::atomic(const FunctionEntity& function, const ast::CallExpr* call,
                                              const Arguments& arguments) {
    const TypeInfo callType = types_.describe(call->type());
    const auto result = [&](const value::Value& value, ast::QualType type) -> std::optional<value::Value> {
        if (callType.representation == Representation::Void) {
            return std::nullopt;
        }
        return convert(value, types_.describe(type), callType, call);
    };
    // A comparison and its conversion to the type of the call, without a term kind.
    const auto boolean = [&](bool holds, bool dependsOnInputs) -> std::optional<value::Value> {
        if (callType.representation == Representation::Void) {
            return std::nullopt;
        }
        value::Value truth = value::Value::integer(holds ? 1 : 0, callType.integer);
        truth.setTerm(opaqueIf(dependsOnInputs));
        return truth;
    };
    if (function.builtin == Builtin::AtomicNoOp) {
        return result(value::Value::integer(0, types_.intFormat()), context_.builtin(ast::BuiltinKind::Int));
    }
    if (function.builtin == Builtin::AtomicLockFree) {
        return boolean(true, false);
    }
    const std::size_t needed = [&]() -> std::size_t {
        switch (function.builtin) {
        case Builtin::AtomicLoad:
        case Builtin::AtomicTestAndSet:
        case Builtin::AtomicClear:
        case Builtin::SyncLockRelease:
            return 1;
        case Builtin::AtomicCompareExchange:
        case Builtin::AtomicCompareExchangeGeneric:
        case Builtin::SyncBoolCompareSwap:
        case Builtin::SyncValCompareSwap:
        case Builtin::AtomicExchangeGeneric:
            return 3;
        default:
            return 2;
        }
    }();
    if (arguments.values.size() < needed
        || types_.describe(arguments.types[0]).representation != Representation::Pointer) {
        undefined(std::format("wrong arguments to '{}'", function.name.text()), call);
    }
    const LValue object{.address = static_cast<Address>(arguments.values[0].bits())};
    const ast::QualType type = TypeTable::pointee(arguments.types[0]);
    const TypeInfo info = types_.describe(type);
    const auto operand = [&](std::size_t i) {
        return convert(arguments.values[i], arguments.types[i], type, call);
    };
    const auto pointer = [&](std::size_t i) {
        return LValue{.address = static_cast<Address>(arguments.values[i].bits())};
    };

    switch (function.builtin) {
    case Builtin::AtomicLoad:
        return result(load(object, type, call), type);
    case Builtin::AtomicStore:
        (void)store(object, type, operand(1), call);
        return std::nullopt;
    case Builtin::AtomicExchange: {
        const value::Value old = load(object, type, call);
        (void)store(object, type, operand(1), call);
        return result(old, type);
    }
    case Builtin::AtomicCompareExchange: {
        const value::Value current = load(object, type, call);
        const value::Value expected = load(pointer(1), type, call);
        const bool isEqual = current.bits() == expected.bits();
        if (isEqual) {
            (void)store(object, type, operand(2), call);
        } else {
            (void)store(pointer(1), type, current, call);
        }
        return boolean(isEqual, current.term() || expected.term());
    }
    case Builtin::AtomicLoadGeneric:
        if (const auto copied = memory_.copy(pointer(1).address, object.address, info.size); !copied) {
            memoryError(copied.error(), true, call);
        }
        return std::nullopt;
    case Builtin::AtomicStoreGeneric:
        if (const auto copied = memory_.copy(object.address, pointer(1).address, info.size); !copied) {
            memoryError(copied.error(), true, call);
        }
        return std::nullopt;
    case Builtin::AtomicExchangeGeneric: {
        const value::Value old = loadRecord(object.address, info.size, call);
        if (const auto copied = memory_.copy(object.address, pointer(1).address, info.size); !copied) {
            memoryError(copied.error(), true, call);
        }
        writeRecord(pointer(2).address, old.asBytes(), old.term(), call);
        return std::nullopt;
    }
    case Builtin::AtomicCompareExchangeGeneric: {
        const value::Value current = loadRecord(object.address, info.size, call);
        const value::Value expected = loadRecord(pointer(1).address, info.size, call);
        const bool isEqual = std::ranges::equal(current.asBytes(), expected.asBytes());
        if (isEqual) {
            if (const auto copied = memory_.copy(object.address, pointer(2).address, info.size); !copied) {
                memoryError(copied.error(), true, call);
            }
        } else {
            writeRecord(pointer(1).address, current.asBytes(), current.term(), call);
        }
        return boolean(isEqual, current.term() || expected.term());
    }
    case Builtin::AtomicFetchOp:
    case Builtin::AtomicOpFetch: {
        const value::Value old = load(object, type, call);
        const value::Value argument = operand(1);
        const value::IntegerFormat format = info.integer;
        // Atomic arithmetic wraps, also for signed types: the value that comes with signed overflow, which is also
        // what the term gives.
        const auto apply = [&](value::IntegerOperator op) {
            value::Value computed = value::integerBinary(op, old, argument, format).value;
            attach(computed, terms_.integerBinary(op, old, argument, format), call);
            return computed;
        };
        value::Value updated;
        switch (function.operation) {
        case BuiltinOperation::Add:
            updated = apply(value::IntegerOperator::Add);
            break;
        case BuiltinOperation::Sub:
            updated = apply(value::IntegerOperator::Sub);
            break;
        case BuiltinOperation::And:
            updated = apply(value::IntegerOperator::And);
            break;
        case BuiltinOperation::Or:
            updated = apply(value::IntegerOperator::Or);
            break;
        case BuiltinOperation::Xor:
            updated = apply(value::IntegerOperator::Xor);
            break;
        case BuiltinOperation::Nand: {
            const value::Value conjunction = apply(value::IntegerOperator::And);
            updated = value::integerComplement(conjunction, format);
            attach(updated, terms_.integerComplement(conjunction, format), call);
            break;
        }
        case BuiltinOperation::Mul:
            NITWIT_UNREACHABLE("no atomic multiplication");
        }
        (void)store(object, type, updated, call);
        return result(function.builtin == Builtin::AtomicFetchOp ? old : updated, type);
    }
    case Builtin::SyncBoolCompareSwap:
    case Builtin::SyncValCompareSwap: {
        const value::Value current = load(object, type, call);
        const value::Value expected = operand(1);
        const bool isEqual = current.bits() == expected.bits();
        if (isEqual) {
            (void)store(object, type, operand(2), call);
        }
        if (function.builtin == Builtin::SyncBoolCompareSwap) {
            return boolean(isEqual, current.term() || expected.term());
        }
        return result(current, type);
    }
    case Builtin::AtomicTestAndSet: {
        const ast::QualType byte = context_.builtin(ast::BuiltinKind::UnsignedChar);
        const value::Value old = load(object, byte, call);
        (void)store(object, byte, value::Value::integer(1, types_.describe(byte).integer), call);
        return boolean(old.bits() != 0, old.term().has_value());
    }
    case Builtin::AtomicClear: {
        const ast::QualType byte = context_.builtin(ast::BuiltinKind::UnsignedChar);
        (void)store(object, byte, value::Value::integer(0, types_.describe(byte).integer), call);
        return std::nullopt;
    }
    case Builtin::SyncLockRelease:
        (void)store(object, type, zeroOf(info), call);
        return std::nullopt;
    default:
        break;
    }
    NITWIT_UNREACHABLE("not an atomic builtin");
}

} // namespace nitwit::interp
