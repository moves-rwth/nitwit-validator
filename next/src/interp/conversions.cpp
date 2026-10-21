// Conversions (C17 6.3): the cast kinds sema makes explicit, the conversions of assignments and arguments, and which
// conversions keep the origin of a nondeterministic value.

#include <algorithm>

#include "ast/casting.hpp"
#include "interp/execution.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

/// The bits of the significand, which bound the integers a floating format represents exactly.
unsigned significandBits(value::FloatFormat format) {
    switch (format) {
    case value::FloatFormat::Float:
        return 24;
    case value::FloatFormat::Double:
        return 53;
    case value::FloatFormat::LongDouble:
        return 64;
    }
    return 0;
}

/// Whether converting from one type to the other preserves every value, so that the result is still an unmodified
/// copy of the same input (interp/README.md, "Origins").
bool preservesValues(const TypeInfo& from, const TypeInfo& to) {
    if (to.representation == Representation::Bool) {
        return from.representation == Representation::Bool;
    }
    if (from.isIntegral() && to.isIntegral()) {
        const value::IntegerFormat a = from.integer;
        const value::IntegerFormat b = to.integer;
        if (from.representation == Representation::Pointer && b.bits > a.bits) {
            return false; // sign-extended, see convert
        }
        return b.bits >= a.bits && (a.isSigned == b.isSigned || (!a.isSigned && b.bits > a.bits));
    }
    if (from.isIntegral() && to.representation == Representation::Floating) {
        return static_cast<unsigned>(from.integer.bits - (from.integer.isSigned ? 1 : 0))
               <= significandBits(to.floating);
    }
    if (from.representation == Representation::Floating && to.representation == Representation::Floating) {
        return significandBits(to.floating) >= significandBits(from.floating);
    }
    return false;
}

} // namespace

value::Value Execution::castValue(ast::CastKind kind, const ast::Expr* operand, ast::QualType to,
                                  const ast::Expr* where) {
    switch (kind) {
    case ast::CastKind::LValueToRValue:
        return load(lvalue(operand), operand->type(), operand);
    case ast::CastKind::ArrayToPointerDecay:
    case ast::CastKind::FunctionToPointerDecay:
        // C17 6.3.2.1p3-4: the address of the first element, or of the function.
        return value::Value::integer(lvalue(operand).address, types_.pointerFormat());
    case ast::CastKind::ToVoid:
        effect(operand);
        return {};
    case ast::CastKind::NullToPointer:
        effect(operand);
        return value::Value::integer(0, types_.pointerFormat());
    case ast::CastKind::NoOp:
    case ast::CastKind::BitCast:
        return rvalue(operand);
    case ast::CastKind::ToUnion: {
        // GNU: the union whose member of the operand's type holds the value; all members start at offset 0.
        const value::Value member = rvalue(operand);
        const TypeInfo memberType = types_.describe(operand->type());
        std::vector<std::byte> bytes(types_.describe(to).size);
        if (memberType.isBytes()) {
            const std::span<const std::byte> source = member.asBytes();
            std::copy_n(source.begin(), std::min(source.size(), bytes.size()), bytes.begin());
        } else if (memberType.representation == Representation::Floating) {
            value::encodeFloat(member, memberType.floating, std::span(bytes).first(memberType.size));
        } else {
            value::encodeInteger(member, memberType.integer, std::span(bytes).first(memberType.size));
        }
        return value::Value::ofBytes(std::move(bytes));
    }
    case ast::CastKind::IntegralCast:
    case ast::CastKind::IntegralToBoolean:
    case ast::CastKind::IntegralToFloating:
    case ast::CastKind::IntegralToPointer:
    case ast::CastKind::PointerToIntegral:
    case ast::CastKind::PointerToBoolean:
    case ast::CastKind::FloatingCast:
    case ast::CastKind::FloatingToIntegral:
    case ast::CastKind::FloatingToBoolean:
        return convert(rvalue(operand), types_.describe(operand->type()), types_.describe(to), where);
    }
    NITWIT_UNREACHABLE("unknown cast kind");
}

value::Value Execution::convert(const value::Value& value, ast::QualType from, ast::QualType to,
                                const ast::Expr* where) {
    return convert(value, types_.describe(from), types_.describe(to), where);
}

value::Value Execution::convert(const value::Value& value, const TypeInfo& from, const TypeInfo& to,
                                const ast::Expr* where) {
    if (!from.isScalar() || !to.isScalar()) {
        return value; // records and va_lists of the same type
    }
    value::Value result;
    if (to.representation == Representation::Bool) {
        result = value::toBool(value); // C17 6.3.1.2
    } else if (to.representation == Representation::Floating) {
        if (from.representation == Representation::Floating) {
            if (from.floating == to.floating) {
                return value;
            }
            result = value::convertFloat(value, from.floating, to.floating); // C17 6.3.1.5
        } else {
            result = value::integerToFloat(value, from.integer, to.floating); // C17 6.3.1.4p2
        }
    } else if (from.representation == Representation::Floating) {
        // C17 6.3.1.4p1: truncation toward zero; out of range is undefined, with the hardware's result.
        result = checked(value::floatToInteger(value, from.floating, to.integer, types_.target().dataModel()), where);
    } else {
        if (from.integer == to.integer) {
            return value;
        }
        value::Value source = value;
        if (from.representation == Representation::Pointer && to.integer.bits > from.integer.bits) {
            // GCC sign-extends a pointer converted to a wider integer type ("Arrays and pointers").
            source = value::Value::integer(value.bits(), {.bits = from.integer.bits, .isSigned = true});
        }
        result = value::convertInteger(source, to.integer); // C17 6.3.1.3, modulo 2^N as GCC defines it
    }
    if (preservesValues(from, to)) {
        result.setOrigin(value.origin());
    }
    return result;
}

value::Value Execution::zeroOf(const TypeInfo& type) const {
    switch (type.representation) {
    case Representation::Integer:
    case Representation::Bool:
    case Representation::Pointer:
        return value::Value::integer(0, type.integer);
    case Representation::Floating:
        switch (type.floating) {
        case value::FloatFormat::Float:
            return value::Value::ofFloat(0.0F);
        case value::FloatFormat::Double:
            return value::Value::ofDouble(0.0);
        case value::FloatFormat::LongDouble:
            return value::Value::ofLongDouble(0.0L);
        }
        break;
    case Representation::Record:
    case Representation::VaList:
    case Representation::Complex:
        return value::Value::ofBytes(std::vector<std::byte>(type.size));
    default:
        break;
    }
    return {};
}

} // namespace nitwit::interp
