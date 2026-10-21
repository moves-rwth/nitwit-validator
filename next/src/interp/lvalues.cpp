// Lvalues (C17 6.3.2.1): the objects expressions designate, and loads and stores of their values, including
// bit-fields, uninitialized reads and the origins of nondeterministic values.

#include <array>
#include <format>

#include "ast/casting.hpp"
#include "interp/execution.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

/// The bytes that hold a bit-field: from the byte of its first bit to the byte of its last.
std::uint64_t storageBytes(const LValue& object) {
    return (object.bitOffset + object.bitWidth + 7) / 8;
}

/// A scalar's value from its object representation.
value::Value decode(std::span<const std::byte> bytes, const TypeInfo& type) {
    return type.representation == Representation::Floating ? value::decodeFloat(bytes, type.floating)
                                                           : value::decodeInteger(bytes, type.integer);
}

void encode(const value::Value& value, const TypeInfo& type, std::span<std::byte> out) {
    if (type.representation == Representation::Floating) {
        value::encodeFloat(value, type.floating, out);
    } else {
        value::encodeInteger(value, type.integer, out);
    }
}

} // namespace

LValue Execution::lvalue(const ast::Expr* expr) {
    checkHostStack(expr); // deep postfix chains recurse here
    switch (expr->kind()) {
    case ast::ExprKind::DeclRef: {
        const ast::ValueDecl* decl = ast::cast<ast::DeclRefExpr>(expr)->decl();
        if (const auto* var = ast::dyn_cast<ast::VarDecl>(decl)) {
            return {.address = variableAddress(var, expr)};
        }
        if (const auto* function = ast::dyn_cast<ast::FunctionDecl>(decl)) {
            return {.address = functionAddress(function)};
        }
        break;
    }
    case ast::ExprKind::Paren:
        return lvalue(ast::cast<ast::ParenExpr>(expr)->inner());
    case ast::ExprKind::Unary: {
        const auto* unaryExpr = ast::cast<ast::UnaryExpr>(expr);
        if (unaryExpr->op() == ast::UnaryOperator::Dereference) {
            return {.address = static_cast<Address>(rvalue(unaryExpr->operand()).bits())};
        }
        if (unaryExpr->op() == ast::UnaryOperator::Extension) {
            return lvalue(unaryExpr->operand());
        }
        break;
    }
    case ast::ExprKind::ArraySubscript: {
        // C17 6.5.2.1p2: E1[E2] is *((E1) + (E2)); sema made E1 the pointer.
        const auto* subscript = ast::cast<ast::ArraySubscriptExpr>(expr);
        const value::Value base = rvalue(subscript->base());
        const value::Value index = rvalue(subscript->index());
        return {.address =
                    static_cast<Address>(pointerOffset(base, index, subscript->base()->type(), false, expr).bits())};
    }
    case ast::ExprKind::Member:
        return member(ast::cast<ast::MemberExpr>(expr));
    case ast::ExprKind::CompoundLiteral:
        return compoundLiteral(ast::cast<ast::CompoundLiteralExpr>(expr));
    case ast::ExprKind::StringLiteral:
        return {.address = stringAddress(ast::cast<ast::StringLiteralExpr>(expr))};
    case ast::ExprKind::GenericSelection:
        return lvalue(ast::cast<ast::GenericSelectionExpr>(expr)->selected());
    case ast::ExprKind::ChooseExpr:
        return lvalue(ast::cast<ast::ChooseExpr>(expr)->selected());
    case ast::ExprKind::ImplicitCast: {
        const auto* castExpr = ast::cast<ast::ImplicitCastExpr>(expr);
        if (castExpr->castKind() == ast::CastKind::NoOp && castExpr->operand()->isLValue()) {
            return lvalue(castExpr->operand());
        }
        break;
    }
    default:
        break;
    }
    stop(Termination::Unsupported, "an expression that designates no object the interpreter can address",
         expr->location());
}

Address Execution::variableAddress(const ast::VarDecl* decl, const ast::Expr* where) {
    if (decl->hasStaticStorage()) {
        const std::optional<GlobalId> global = code_.findGlobal(decl);
        if (!global) {
            stop(Termination::Unsupported, std::format("'{}' has no storage", decl->name().text()), locationOf(where));
        }
        return globalAddresses_[*global];
    }
    const std::uint32_t index = decl->localIndex();
    // Only an expression from outside the function, such as a witness constraint, can refer to another function's
    // variable.
    if (frame_ == nullptr || index >= frame_->locals.size()
        || (isEvaluatingConstraint_ && frame_->body->definition->locals()[index] != decl)) {
        stop(Termination::Unsupported,
             std::format("'{}' is not a variable of the current function", decl->name().text()), locationOf(where));
    }
    return frame_->locals[index];
}

Address Execution::functionAddress(const ast::FunctionDecl* decl) {
    const FunctionEntity& function = followAlias(entityOf(decl));
    return functionAddresses_[static_cast<FunctionId>(&function - code_.functions.data())];
}

// C17 6.5.2.3: `.` designates a member of the object; `->` one of the object the pointer points to.
LValue Execution::member(const ast::MemberExpr* expr) {
    const ast::FieldDecl* field = expr->field();
    const ast::RecordLayout& layout = sema::layoutOf(field->parent());
    Address base = 0;
    if (expr->isArrow()) {
        base = static_cast<Address>(rvalue(expr->base()).bits());
    } else if (expr->base()->isLValue()) {
        base = lvalue(expr->base()).address;
    } else {
        base = recordObject(expr->base()); // `f().x`, `(c ? s : t).a[1]`
    }
    const auto address = static_cast<Address>(
        value::Value::integer(UInt128{base} + layout.fieldOffsets[field->index()], types_.pointerFormat()).bits());
    if (field->isBitField()) {
        return {.address = address, .bitWidth = field->bitWidth(), .bitOffset = layout.bitOffsets[field->index()]};
    }
    return {.address = address};
}

Address Execution::recordObject(const ast::Expr* expr) {
    expr = ast::ignoreParens(expr);
    switch (expr->kind()) {
    case ast::ExprKind::ImplicitCast: {
        const auto* castExpr = ast::cast<ast::ImplicitCastExpr>(expr);
        if (castExpr->castKind() == ast::CastKind::LValueToRValue) {
            return lvalue(castExpr->operand()).address;
        }
        if (castExpr->castKind() == ast::CastKind::NoOp) {
            return recordObject(castExpr->operand());
        }
        break;
    }
    case ast::ExprKind::Cast: {
        const auto* castExpr = ast::cast<ast::CastExpr>(expr);
        if (castExpr->castKind() == ast::CastKind::NoOp) {
            return recordObject(castExpr->operand());
        }
        break;
    }
    case ast::ExprKind::Call: {
        Address object = 0;
        const value::Value result = call(ast::cast<ast::CallExpr>(expr), true, &object);
        if (object != 0) {
            return object;
        }
        const Address copy = temporary(result.asBytes().size(), nullptr, expr);
        writeBytes(copy, result.asBytes(), expr);
        return copy;
    }
    case ast::ExprKind::Conditional: {
        value::Value condition;
        const ast::Expr* selected = selectOperand(ast::cast<ast::ConditionalExpr>(expr), condition);
        NITWIT_ASSERT(selected != nullptr, "GNU ?: with a struct condition");
        return recordObject(selected);
    }
    case ast::ExprKind::Binary: {
        const auto* binaryExpr = ast::cast<ast::BinaryExpr>(expr);
        if (binaryExpr->op() == ast::BinaryOperator::Comma) {
            effect(binaryExpr->lhs());
            return recordObject(binaryExpr->rhs());
        }
        if (binaryExpr->op() == ast::BinaryOperator::Assign) {
            return assignRecord(binaryExpr);
        }
        break;
    }
    default:
        break;
    }
    const value::Value result = rvalue(expr);
    const Address copy = temporary(result.asBytes().size(), nullptr, expr);
    writeBytes(copy, result.asBytes(), expr);
    return copy;
}

Address Execution::temporary(std::uint64_t size, Frame* owner, const ast::Expr* where) {
    if (isEvaluatingConstraint_) {
        stop(Termination::Unsupported, "a constraint must not create a temporary object", locationOf(where));
    }
    const Address address = allocate(Region::Heap, size, types_.target().maxAlign(), "temporary", where);
    if (Frame* frame = owner != nullptr ? owner : frame_; frame != nullptr) {
        frame->temporaries.push_back(address);
    }
    return address;
}

void Execution::copyObject(Address destination, Address source, std::uint64_t size, const ast::Expr* where) {
    if (const auto copied = memory_.copy(destination, source, size); !copied) {
        // Memory::copy checks the source first.
        memoryError(copied.error(), copied.error().address != source, where);
    }
}

void Execution::releaseTemporaries(Frame& frame) {
    for (const Address address : frame.temporaries) {
        (void)memory_.free(address);
    }
    frame.temporaries.clear();
}

value::Value Execution::load(const LValue& object, ast::QualType type, const ast::Expr* site) {
    const TypeInfo info = types_.describe(type);
    if (object.isBitField()) {
        return loadBitField(object, info, site);
    }
    if (info.isScalar()) {
        std::array<std::byte, 16> buffer{};
        const std::span<std::byte> bytes(buffer.data(), info.size);
        const auto read = memory_.readScalar(object.address, bytes);
        if (!read) {
            memoryError(read.error(), false, site);
        }
        if (!read->isInitialized) {
            // An uninitialized automatic or heap object holds a nondeterministic value, as the verifiers assume. The
            // answer is stored, so that later reads agree; bytes that were written keep their values.
            value::Value input = requestInput(InputRequest::Kind::UninitializedRead, site, type, nullptr);
            std::array<std::byte, 16> provided{};
            encode(input, info, std::span(provided.data(), info.size));
            bool isPartlyInitialized = false;
            for (std::size_t i = 0; i < info.size; ++i) {
                if (memory_.isInitialized(object.address + i, 1)) {
                    provided[i] = bytes[i];
                    isPartlyInitialized = true;
                }
            }
            writeBytes(object.address, std::span(provided.data(), info.size), site);
            if (isPartlyInitialized) {
                return decode(std::span(provided.data(), info.size), info);
            }
            memory_.setOrigin(object.address, info.size, *input.origin());
            return input;
        }
        value::Value result = decode(bytes, info);
        result.setOrigin(read->origin);
        return result;
    }
    if (info.isBytes()) {
        return value::Value::ofBytes(readBytes(object.address, info.size, site));
    }
    stop(Termination::Unsupported, "a read of an object that is not a scalar or a record", locationOf(site));
}

// A bit-field with uninitialized storage: the uninitialized bytes of its storage are inputs, as the bytes of any
// uninitialized object, and the field is read from them.
value::Value Execution::loadBitField(const LValue& object, const TypeInfo& type, const ast::Expr* site) {
    std::array<std::byte, 16> storage{};
    const std::span<std::byte> bytes(storage.data(), storageBytes(object));
    const auto read = memory_.readScalar(object.address, bytes);
    if (!read) {
        memoryError(read.error(), false, site);
    }
    if (!read->isInitialized) {
        const ast::QualType byte = context_.builtin(ast::BuiltinKind::UnsignedChar);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (!memory_.isInitialized(object.address + i, 1)) {
                const value::Value input = requestInput(InputRequest::Kind::UninitializedRead, site, byte, nullptr);
                bytes[i] = static_cast<std::byte>(input.bits() & 0xFF);
            }
        }
        writeBytes(object.address, bytes, site);
    }
    return value::readBitField(bytes, object.bitOffset, object.bitWidth, type.integer);
}

value::Value Execution::store(const LValue& object, ast::QualType type, const value::Value& stored,
                              const ast::Expr* where) {
    const TypeInfo info = types_.describe(type);
    if (object.isBitField()) {
        // C17 6.7.2.1p10: the other bits of the storage unit keep their values.
        std::array<std::byte, 16> storage{};
        const std::span<std::byte> bytes(storage.data(), storageBytes(object));
        if (const auto read = memory_.read(object.address, bytes); !read) {
            memoryError(read.error(), true, where);
        }
        value::writeBitField(bytes, object.bitOffset, object.bitWidth, stored);
        writeBytes(object.address, bytes, where);
        return value::readBitField(bytes, object.bitOffset, object.bitWidth, info.integer);
    }
    if (info.isScalar()) {
        std::array<std::byte, 16> buffer{};
        const std::span<std::byte> bytes(buffer.data(), info.size);
        encode(stored, info, bytes);
        writeBytes(object.address, bytes, where);
        if (const std::optional<value::InputId> origin = stored.origin()) {
            memory_.setOrigin(object.address, info.size, *origin);
        }
        return stored;
    }
    if (info.isBytes()) {
        const std::span<const std::byte> bytes = stored.asBytes();
        writeBytes(object.address, bytes.first(std::min<std::size_t>(bytes.size(), info.size)), where);
        return stored;
    }
    stop(Termination::Unsupported, "a write of an object that is not a scalar or a record", locationOf(where));
}

void Execution::storeRecord(Address destination, std::uint64_t size, const ast::Expr* source) {
    copyObject(destination, recordObject(source), size, source);
}

std::vector<std::byte> Execution::readBytes(Address address, std::uint64_t size, const ast::Expr* where) {
    // Checks the range before the host allocates the buffer, which a wild size could make huge.
    const std::optional<AllocationInfo> allocation = memory_.find(address);
    if (size > 0 && (!allocation || size > allocation->base + allocation->size - address)) {
        std::array<std::byte, 1> probe{};
        const auto read = memory_.read(address, probe);
        memoryError(read ? MemoryError{.kind = MemoryErrorKind::OutOfBounds, .address = address, .size = size}
                         : read.error(),
                    false, where);
    }
    std::vector<std::byte> bytes(size);
    if (const auto read = memory_.read(address, bytes); !read) {
        memoryError(read.error(), false, where);
    }
    return bytes;
}

void Execution::writeBytes(Address address, std::span<const std::byte> bytes, const ast::Expr* where) {
    if (const auto written = memory_.write(address, bytes); !written) {
        memoryError(written.error(), true, where);
    }
}

std::uint64_t Execution::sizeOf(ast::QualType type, const ast::Expr* where) {
    const TypeInfo info = types_.describe(type);
    if (!info.isVariableArray) {
        return info.size;
    }
    const ast::ArrayType* array = TypeTable::array(type);
    return arraySize(array) * sizeOf(array->element(), where);
}

// C17 6.7.6.2p5: the size of a variable length array type is fixed when its declaration is reached; a type written
// elsewhere, as in sizeof(int[n]), is evaluated where it is used.
std::uint64_t Execution::arraySize(const ast::ArrayType* array) {
    switch (array->sizeKind()) {
    case ast::ArrayType::SizeKind::Constant:
        return array->size();
    case ast::ArrayType::SizeKind::Variable:
        break;
    default:
        return 0;
    }
    if (frame_ != nullptr) {
        for (auto known = frame_->arraySizes.rbegin(); known != frame_->arraySizes.rend(); ++known) {
            if (known->first == array) {
                return known->second;
            }
        }
    }
    return static_cast<std::uint64_t>(rvalue(array->sizeExpression()).bits());
}

} // namespace nitwit::interp
