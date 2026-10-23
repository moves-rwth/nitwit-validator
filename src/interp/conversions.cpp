// Conversions (C17 6.3): the cast kinds sema makes explicit, and the conversions of assignments and arguments, with
// the terms that record them.

#include <algorithm>

#include "ast/casting.hpp"
#include "interp/execution.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

value::Value Execution::castValue(ast::CastKind kind, const ast::Expr* operand, ast::QualType to,
                                  const ast::Expr* where) {
    switch (kind) {
    case ast::CastKind::LValueToRValue:
        return load(lvalue(operand), operand->type(), operand);
    case ast::CastKind::ArrayToPointerDecay:
    case ast::CastKind::FunctionToPointerDecay:
        // C17 6.3.2.1p3-4: the address of the first element, or of the function.
        return pointerTo(lvalue(operand));
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
        value::Value result = value::Value::ofBytes(std::move(bytes));
        result.setTerm(opaqueIf(member.term().has_value())); // loadRecord's term of a record value
        return result;
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
    if (to.representation == Representation::Bool) {
        return toBool(value, from.scalarFormat(), where); // C17 6.3.1.2
    }
    if (from.representation == Representation::Floating || to.representation == Representation::Floating) {
        if (from.representation == to.representation && from.floating == to.floating) {
            return value;
        }
        return convertFloating(value, from, to, where);
    }
    if (from.representation == Representation::Pointer && to.integer.bits > from.integer.bits) {
        // GCC sign-extends a pointer converted to a wider integer type ("Arrays and pointers"): the conversion to the
        // signed integer of the pointer's width comes first.
        const value::IntegerFormat signedPointer{.bits = from.integer.bits, .isSigned = true};
        return convertInteger(convertInteger(value, from.integer, signedPointer, where), signedPointer, to.integer,
                              where);
    }
    // C17 6.3.1.3, modulo 2^N as GCC defines it
    return convertInteger(value, from.integer, to.integer, where);
}

// The conversion functions return one named value each, so that it is constructed in place (NRVO).

value::Value Execution::convertInteger(const value::Value& value, value::IntegerFormat from, value::IntegerFormat to,
                                       const ast::Expr* where) {
    value::Value result = from == to ? value : value::convertInteger(value, to);
    if (from != to && value.term()) {
        attach(result, terms_.convertInteger(value, from, to), where);
    }
    return result;
}

// _Bool has the format of unsigned char: of the terms of that format, only a conversion to _Bool is known to give 0 or
// 1 for every input. The conversion to _Bool of such a term, also behind integer conversions, which keep 0 and 1 (the
// promotion of a _Bool to int, as in `c ? b : d`), is that term (interp/README.md, "Terms").
value::Value Execution::toBool(const value::Value& value, value::ScalarFormat from, const ast::Expr* where) {
    value::Value result = value::toBool(value);
    if (const std::optional<value::TermId> term = value.term()) {
        value::TermId operand = *term;
        while (terms_.term(operand).kind == value::TermKind::ConvertInteger) {
            operand = terms_.term(operand).operands[0];
        }
        attach(result, terms_.term(operand).kind == value::TermKind::ToBool ? operand : terms_.toBool(value, from),
               where);
    }
    return result;
}

// C17 6.3.1.5 between floating types, 6.3.1.4p2 from integers, 6.3.1.4p1 to integers: truncation toward zero, and out
// of range undefined, with the hardware's result.
value::Value Execution::convertFloating(const value::Value& value, const TypeInfo& from, const TypeInfo& to,
                                        const ast::Expr* where) {
    const bool isFromFloating = from.representation == Representation::Floating;
    const bool isToFloating = to.representation == Representation::Floating;
    value::Value result =
        !isFromFloating ? value::integerToFloat(value, from.integer, to.floating)
        : isToFloating
            ? value::convertFloat(value, from.floating, to.floating)
            : checked(value::floatToInteger(value, from.floating, to.integer, types_.target().dataModel()), where);
    if (value.term()) {
        attach(result,
               !isFromFloating ? terms_.integerToFloat(value, from.integer, to.floating)
               : isToFloating  ? terms_.convertFloat(value, from.floating, to.floating)
                               : terms_.floatToInteger(value, from.floating, to.integer),
               where);
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
