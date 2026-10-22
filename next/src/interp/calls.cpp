// Calls: the callee, the arguments, frames, and what functions without a definition do (library, oracle).

#include <format>
#include <utility>

#include "ast/casting.hpp"
#include "interp/execution.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

/// Builtins whose arguments are not plain values: lvalues (va_list), unevaluated operands, or types.
bool hasSpecialArguments(const FunctionEntity& function) {
    if (function.kind != FunctionKind::Builtin) {
        return false;
    }
    switch (function.builtin) {
    case Builtin::VaStart:
    case Builtin::VaEnd:
    case Builtin::VaCopy:
    case Builtin::ConstantP:
    case Builtin::OverflowP:
        return true;
    default:
        return false;
    }
}

/// C17 6.5.2.2p6: whether an argument passed through a declaration without prototype (so with the default argument
/// promotions) fits a parameter of a function defined with a prototype: the promoted types must be compatible, or a
/// signed and the corresponding unsigned integer type with the value representable in both. Pointers count as
/// compatible with pointers, records with records of the same size.
bool passesWithoutPrototype(const TypeInfo& argument, const value::Value& value, TypeInfo parameter) {
    if (parameter.isIntegral() && parameter.representation != Representation::Pointer && parameter.integer.bits < 32) {
        parameter.integer = {.bits = 32, .isSigned = true}; // C17 6.3.1.1p2
        parameter.representation = Representation::Integer;
    }
    if (parameter.representation == Representation::Floating && parameter.floating == value::FloatFormat::Float) {
        parameter.floating = value::FloatFormat::Double;
    }
    if (argument.representation == Representation::Pointer || parameter.representation == Representation::Pointer) {
        return argument.representation == parameter.representation;
    }
    if (argument.isIntegral() && parameter.isIntegral()) {
        if (argument.integer.bits != parameter.integer.bits) {
            return false;
        }
        if (argument.integer.isSigned == parameter.integer.isSigned) {
            return true;
        }
        // The value is representable in both if it is non-negative in the signed type.
        return value::Value::integer(value.bits(), {.bits = argument.integer.bits, .isSigned = true}).signedBits() >= 0;
    }
    if (argument.representation == Representation::Floating && parameter.representation == Representation::Floating) {
        return argument.floating == parameter.floating;
    }
    return argument.isBytes() && parameter.isBytes() && argument.size == parameter.size;
}

value::FloatFormat formatOf(value::Value::Kind kind) {
    switch (kind) {
    case value::Value::Kind::Float:
        return value::FloatFormat::Float;
    case value::Value::Kind::LongDouble:
        return value::FloatFormat::LongDouble;
    default:
        return value::FloatFormat::Double;
    }
}

} // namespace

value::Value Execution::call(const ast::CallExpr* expr, bool isValueUsed, Address* object) {
    if (isEvaluatingConstraint_) {
        stop(Termination::Unsupported, "a constraint must not call a function", expr->location());
    }
    const FunctionEntity* callee = resolveCallee(expr);
    Arguments arguments;
    if (callee == nullptr || !hasSpecialArguments(*callee)) {
        evaluateArguments(expr, arguments);
    }
    countStep();
    const ast::FunctionDecl* calleeDecl = callee != nullptr ? callee->decl : nullptr;
    if (isReporting_) {
        obey(observer_.afterArguments({.call = expr, .callee = calleeDecl, .arguments = arguments.values}, *this),
             expr->rightParenLocation());
    }
    if (callee == nullptr) {
        undefined("a call through a pointer that points to no function", expr);
    }

    std::optional<value::Value> result;
    switch (callee->kind) {
    case FunctionKind::Defined: {
        Returned returned = invoke(*callee, arguments, expr);
        result = std::move(returned.value);
        if (object != nullptr) {
            *object = returned.object;
        }
        break;
    }
    case FunctionKind::Library:
        result = callLibrary(*callee, arguments, expr);
        break;
    case FunctionKind::Builtin:
        result = hasSpecialArguments(*callee) ? builtinOnExpressions(*callee, expr)
                                              : builtinOnValues(*callee, expr, arguments);
        break;
    case FunctionKind::Nondet:
        result = unknownResult(InputRequest::Kind::NondetCall, expr, *callee);
        break;
    case FunctionKind::Unknown:
        // A function without definition: its return value is nondeterministic and its arguments have no effect.
        result = unknownResult(InputRequest::Kind::UndefinedFunction, expr, *callee);
        break;
    case FunctionKind::WeakMissing:
        undefined(std::format("a call of the weak function '{}', which has no definition", callee->name.text()), expr);
    case FunctionKind::Alias:
        NITWIT_UNREACHABLE("aliases are resolved before the call");
    }

    const TypeInfo type = types_.describe(expr->type());
    if (result.has_value()) {
        result = inCallType(std::move(*result), callee->name.text(), type, expr);
    }
    if (isReporting_) {
        returnValue_ = result.has_value() ? &*result : nullptr;
        const Directive directive =
            observer_.afterReturn({.call = expr, .callee = calleeDecl, .result = returnValue_}, *this);
        returnValue_ = nullptr;
        obey(directive, expr->rightParenLocation());
    }
    if (!result.has_value()) {
        // C17 6.9.1p12: using the value of a function that reached its closing brace is undefined.
        if (isValueUsed && type.representation != Representation::Void) {
            undefined(
                std::format("the value of '{}' is used, but it ended without returning a value", callee->name.text()),
                expr);
        }
        return {};
    }
    return std::move(*result);
}

// A function called through a pointer of another type (C17 6.5.2.2p9), or a library function the program declares
// with another type, returns another representation. Integers and floating values are taken as the registers that
// hold them would be; a struct where a scalar is expected, or the other way round, is undefined. The term of an
// integer result tells its format, from which the conversion is recorded.
std::optional<value::Value> Execution::inCallType(value::Value result, std::string_view callee, const TypeInfo& type,
                                                  const ast::Expr* where) {
    if (type.representation == Representation::Void) {
        return std::nullopt;
    }
    switch (result.kind()) {
    case value::Value::Kind::Integer: {
        // A result with a term (other than the opaque term) has the format of its term: the conversion from that
        // format is recorded. The other results are taken as they are.
        const std::optional<value::TermId> term = result.term();
        const std::optional<value::IntegerFormat> from =
            term && !terms_.isOpaque(*term) ? std::optional(std::get<value::IntegerFormat>(terms_.resultFormat(*term)))
                                            : std::nullopt;
        if (type.representation == Representation::Bool) {
            value::Value converted = result.bits() <= 1 ? result : value::toBool(result);
            // _Bool has the format of unsigned char: only a term of a conversion to _Bool is known to give 0 or 1.
            if (from && terms_.term(*term).kind != value::TermKind::ToBool) {
                attach(converted, terms_.toBool(result, *from), where);
            } else {
                converted.setTerm(term);
            }
            return converted;
        }
        if (type.isIntegral()) {
            // The register's bits: also a pointer is zero-extended here.
            if (from) {
                return convertInteger(result, *from, type.integer, where);
            }
            value::Value converted = value::Value::integer(result.bits(), type.integer);
            converted.setTerm(term); // none, or the opaque term
            return converted;
        }
        break;
    }
    case value::Value::Kind::Float:
    case value::Value::Kind::Double:
    case value::Value::Kind::LongDouble:
        if (type.representation == Representation::Floating) {
            const value::FloatFormat format = formatOf(result.kind());
            if (format == type.floating) {
                return result;
            }
            value::Value converted = value::convertFloat(result, format, type.floating);
            attach(converted, terms_.convertFloat(result, format, type.floating), where);
            return converted;
        }
        break;
    case value::Value::Kind::Bytes:
        if (type.isBytes() && result.asBytes().size() == type.size) {
            return result;
        }
        break;
    case value::Value::Kind::None:
        break;
    }
    undefined(std::format("'{}' returns a value that does not fit the type of the call", callee), where);
}

const FunctionEntity* Execution::resolveCallee(const ast::CallExpr* call) {
    if (const ast::FunctionDecl* direct = call->directCallee()) {
        return &followAlias(entityOf(direct));
    }
    const value::Value pointer = rvalue(call->callee());
    return functionAt(static_cast<Address>(pointer.bits()));
}

// GCC evaluates the arguments from right to left on x86 and x86-64 (the order is unspecified, C17 6.5.2.2p10);
// the difftest compares with GCC, and the order shows in the sequence of inputs.
void Execution::evaluateArguments(const ast::CallExpr* call, Arguments& arguments) {
    const std::span<ast::Expr* const> expressions = call->arguments();
    arguments.values.resize(expressions.size());
    arguments.types.resize(expressions.size());
    arguments.objects.assign(expressions.size(), 0);
    for (std::size_t i = expressions.size(); i-- > 0;) {
        const ast::Expr* argument = expressions[i];
        arguments.types[i] = argument->type();
        const TypeInfo type = types_.describe(argument->type());
        if (type.representation != Representation::Record) {
            arguments.values[i] = rvalue(argument);
            continue;
        }
        // A struct or union is copied when the argument is evaluated (later arguments may change the original),
        // with the initialization state and terms of its bytes.
        const Address source = recordObject(argument);
        const Address object = temporary(type.size, nullptr, argument);
        copyObject(object, source, type.size, argument);
        arguments.objects[i] = object;
        arguments.values[i] = loadRecord(object, type.size, argument);
    }
}

Returned Execution::invoke(const FunctionEntity& function, Arguments& arguments, const ast::CallExpr* site) {
    if (frames_.size() >= options_.maxCallDepth) {
        stop(Termination::LimitReached, std::format("the call depth limit of {} was reached", options_.maxCallDepth),
             locationOf(site));
    }
    checkHostStack(site);
    Frame frame;
    enterFrame(frame, function, arguments, site);
    execute(frame, 0, nullptr);
    Returned result = std::move(frame.result);
    leaveFrame(frame);
    return result;
}

void Execution::enterFrame(Frame& frame, const FunctionEntity& function, Arguments& arguments,
                           const ast::CallExpr* site) {
    const FunctionCode& body = *function.code;
    frame.function = &function;
    frame.body = &body;
    frame.serial = nextFrameSerial_++;
    frame.vaCursorBase = vaCursors_.size();
    const std::span<ast::VarDecl* const> locals = body.definition->locals();
    frame.locals.assign(locals.size(), 0);
    for (std::size_t i = 0; i < locals.size(); ++i) {
        const ObjectLayout& layout = body.locals[i];
        if (!layout.isVariableLength) {
            frame.locals[i] = allocate(Region::Stack, layout.size, layout.alignment, locals[i]->name().text(), site);
        }
    }
    frame.literals.reserve(body.compoundLiterals.size());
    for (const ObjectLayout& layout : body.compoundLiteralLayouts) {
        frame.literals.push_back(allocate(Region::Stack, layout.size, layout.alignment, "compound literal", site));
    }
    frames_.push_back(&frame);
    frame_ = &frame;

    // C17 6.5.2.2p4: each parameter is assigned the value of its argument. Through a call without a prototype (K&R
    // definitions, C17 6.9.1p10) or through a pointer of another type, the argument may have another type.
    const std::span<ast::VarDecl* const> parameters = body.definition->parameters();
    const ast::FunctionType* type = TypeTable::function(body.definition->type());
    const ast::FunctionType* called = site != nullptr ? TypeTable::function(site->callee()->type()) : nullptr;
    if (called != nullptr && !called->hasPrototype() && type != nullptr && type->hasPrototype()) {
        checkCallWithoutPrototype(*type, arguments, site);
    }
    for (std::size_t i = 0; i < parameters.size() && i < arguments.values.size(); ++i) {
        const ast::VarDecl* parameter = parameters[i];
        const Address slot = frame.locals[parameter->localIndex()];
        const TypeInfo to = types_.describe(parameter->type());
        const TypeInfo from = types_.describe(arguments.types[i]);
        if (to.isBytes() != from.isBytes() || (to.isBytes() && to.size != from.size)) {
            // Only through a pointer to a function of another type (C17 6.5.2.2p9).
            undefined(std::format("a call of '{}' through a pointer of another type passes argument {} in a type that "
                                  "does not fit the parameter",
                                  function.name.text(), i + 1),
                      site);
        }
        if (to.isBytes()) {
            if (i < arguments.objects.size() && arguments.objects[i] != 0) {
                copyObject(slot, arguments.objects[i], to.size, site);
            } else {
                writeRecord(slot, arguments.values[i].asBytes(), arguments.values[i].term(), site);
            }
        } else {
            (void)store({.address = slot}, parameter->type(), convert(arguments.values[i], from, to, site), site);
        }
    }
    if (type != nullptr && type->isVariadic() && arguments.values.size() > parameters.size()) {
        const auto named = static_cast<std::ptrdiff_t>(parameters.size());
        frame.variadic.values.assign(std::make_move_iterator(arguments.values.begin() + named),
                                     std::make_move_iterator(arguments.values.end()));
        frame.variadic.types.assign(arguments.types.begin() + named, arguments.types.end());
    }
}

// C17 6.5.2.2p6: a call through a declaration without prototype of a function defined with a prototype passes the
// promoted arguments, which the callee reads in its own parameter types (sema/README.md, "Calls through an implicit
// declaration"); that is undefined unless the numbers match and the types fit.
void Execution::checkCallWithoutPrototype(const ast::FunctionType& definition, const Arguments& arguments,
                                          const ast::CallExpr* site) {
    const std::span<const ast::QualType> parameters = definition.parameters();
    const std::size_t count = arguments.values.size();
    if (count < parameters.size() || (count > parameters.size() && !definition.isVariadic())) {
        undefined(std::format("a call without prototype passes {} arguments to a function with {} parameters", count,
                              parameters.size()),
                  site);
    }
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (!passesWithoutPrototype(types_.describe(arguments.types[i]), arguments.values[i],
                                    types_.describe(parameters[i]))) {
            undefined(std::format("a call without prototype passes argument {} in a type that does not fit the "
                                  "parameter",
                                  i + 1),
                      site);
        }
    }
}

void Execution::leaveFrame(Frame& frame) {
    releaseTemporaries(frame);
    // Cursors made in this frame (or in returned callees) walk arguments that end now; their numbers are reused.
    vaCursors_.resize(std::min(vaCursors_.size(), frame.vaCursorBase));
    for (; !frame.dynamic.empty(); frame.dynamic.pop_back()) {
        memory_.popStack(frame.dynamic.back().address);
    }
    frame.arrayCount = 0;
    for (std::size_t i = frame.literals.size(); i-- > 0;) {
        memory_.popStack(frame.literals[i]);
    }
    for (std::size_t i = frame.locals.size(); i-- > 0;) {
        if (!frame.body->locals[i].isVariableLength) {
            memory_.popStack(frame.locals[i]);
        }
    }
    frames_.pop_back();
    frame_ = frames_.empty() ? nullptr : frames_.back();
}

std::optional<value::Value> Execution::callLibrary(const FunctionEntity& function, Arguments& arguments,
                                                   const ast::CallExpr* site) {
    expandVaLists(arguments, site);
    std::vector<ScalarType> argumentTypes;
    argumentTypes.reserve(arguments.types.size());
    for (const ast::QualType type : arguments.types) {
        argumentTypes.push_back(types_.describe(type).scalar);
    }
    const LibraryCall call{.name = function.name.text(), .arguments = arguments.values, .argumentTypes = argumentTypes};
    if (!recordsTerms()) {
        return libraryResult(function, function.library(*this, call), site);
    }
    // Library functions have no term kind (interp/README.md, "Terms"): the result is opaque if it depends on inputs,
    // through an argument or through bytes the function reads. So are the bytes it writes, except that a pointer
    // argument only tells where they are, as the address of a store does, unless the function writes a value computed
    // from a pointer argument (LibraryResult::writesPointerValues).
    memory_.beginRawAccesses();
    LibraryResult result = function.library(*this, call);
    const RawAccesses accesses = memory_.endRawAccesses();
    bool isResultDerived = accesses.readsInputs;
    bool areBytesDerived = accesses.readsInputs;
    for (std::size_t i = 0; i < arguments.values.size(); ++i) {
        if (arguments.values[i].term()) {
            isResultDerived = true;
            areBytesDerived = areBytesDerived || argumentTypes[i] != ScalarType::Pointer || result.writesPointerValues;
        }
    }
    if (areBytesDerived) {
        for (const auto& [address, size] : accesses.writes) {
            memory_.markOpaque(address, size);
        }
    }
    result.value.setTerm(opaqueIf(isResultDerived));
    return libraryResult(function, std::move(result), site);
}

std::optional<value::Value> Execution::libraryResult(const FunctionEntity& function, LibraryResult result,
                                                     const ast::CallExpr* site) {
    switch (result.kind) {
    case LibraryResult::Kind::Return:
        // In the C library's type; call() converts it to the type the program declared.
        if (result.value.kind() == value::Value::Kind::None) {
            return std::nullopt;
        }
        return std::move(result.value);
    case LibraryResult::Kind::Exit:
        throw RunEnd{
            .termination = Termination::Exit, .exitStatus = result.status, .message = {}, .location = site->location()};
    case LibraryResult::Kind::Abort:
        stop(Termination::Abort,
             result.message.empty() ? std::format("{} was called", function.name.text()) : result.message,
             site->location());
    case LibraryResult::Kind::UndefinedBehavior:
        undefined(result.message, site);
    case LibraryResult::Kind::Unsupported:
        stop(Termination::Unsupported, result.message, site->location());
    case LibraryResult::Kind::OutOfMemory:
        stop(Termination::OutOfMemory, result.message.empty() ? "out of memory" : result.message, site->location());
    }
    NITWIT_UNREACHABLE("unknown library result");
}

value::Value Execution::requestInput(InputRequest::Kind kind, const ast::Expr* site, ast::QualType type,
                                     const ast::FunctionDecl* function) {
    const InputRequest request{.kind = kind,
                               .id = value::InputId{inputs_},
                               .site = site,
                               .occurrence = occurrences_[site]++,
                               .type = type.unqualified(),
                               .function = function};
    ++inputs_;
    // The answer in the representation of the type, whatever the oracle gave: a _Bool is 0 or 1 (C17 6.3.1.2), an
    // integer has the width of its type.
    const value::Value provided = oracle_.provide(request);
    const TypeInfo info = types_.describe(type);
    value::Value input = zeroOf(info);
    if (info.representation == Representation::Bool) {
        input = value::toBool(provided);
    } else if (info.isIntegral() && provided.kind() == value::Value::Kind::Integer) {
        input = value::Value::integer(provided.bits(), info.integer);
    } else if (info.representation == Representation::Floating && provided.kind() != value::Value::Kind::Integer
               && provided.kind() != value::Value::Kind::Bytes && provided.kind() != value::Value::Kind::None) {
        input = value::convertFloat(provided, formatOf(provided.kind()), info.floating);
    }
    if (info.isScalar()) {
        input.setTerm(terms_.input(request.id, input, info.scalarFormat()));
    }
    return input;
}

std::optional<value::Value> Execution::unknownResult(InputRequest::Kind kind, const ast::CallExpr* site,
                                                     const FunctionEntity& function) {
    const TypeInfo type = types_.describe(site->type());
    if (type.representation == Representation::Void) {
        return std::nullopt;
    }
    if (!type.isScalar()) {
        return zeroOf(type); // records: the oracle provides scalars only
    }
    return requestInput(kind, site, site->type(), function.decl);
}

const FunctionEntity* Execution::functionAt(Address address) const {
    const auto found = functionsAt_.find(address);
    return found != functionsAt_.end() ? &code_.functions[found->second] : nullptr;
}

const FunctionEntity& Execution::entityOf(const ast::FunctionDecl* decl) const {
    const FunctionEntity* function = code_.findFunction(decl);
    NITWIT_ASSERT(function != nullptr, "a function the lowering did not see");
    return *function;
}

const FunctionEntity& Execution::followAlias(const FunctionEntity& function) const {
    const FunctionEntity* current = &function;
    for (std::size_t hops = 0; current->kind == FunctionKind::Alias && hops < code_.functions.size(); ++hops) {
        current = &code_.functions[current->aliasTarget];
    }
    return *current;
}

} // namespace nitwit::interp
