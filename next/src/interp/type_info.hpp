#pragma once

// Internal to the interpreter: how values of a C type are represented, computed from the type by stripping typedefs,
// without canonicalizing it. The evaluator asks this for every operation, so it must be cheap: no hashing, no
// allocation.

#include <array>
#include <cstdint>

#include "ast/decl.hpp"
#include "ast/type.hpp"
#include "support/target.hpp"
#include "value/value.hpp"

namespace nitwit::interp {

enum class Representation : std::uint8_t {
    Void,
    Integer,  // integer and enumeration types other than _Bool
    Bool,     // _Bool: an unsigned 8-bit integer that holds 0 or 1
    Pointer,  // an unsigned integer of the pointer width
    Floating, // float, double, long double
    Record,   // struct or union: bytes
    Array,
    Function,
    VaList,  // __builtin_va_list: bytes holding the interpreter's handle (see builtins.cpp)
    Complex, // _Complex: bytes; arithmetic is not supported
};

struct TypeInfo {
    Representation representation = Representation::Void;
    value::IntegerFormat integer{};                           // Integer, Bool, Pointer
    value::FloatFormat floating = value::FloatFormat::Double; // Floating
    ScalarType scalar = ScalarType::Int;                      // scalar types: the type itself, for the library
    std::uint64_t size = 0;       // sizeof; 1 for void and functions (GNU); unknown for variable length arrays
    bool isVariableArray = false; // a variable length array, or an array of them

    [[nodiscard]] bool isScalar() const {
        return representation == Representation::Integer || representation == Representation::Bool
               || representation == Representation::Pointer || representation == Representation::Floating;
    }
    /// Integers, _Bool and pointers, whose values are integers.
    [[nodiscard]] bool isIntegral() const {
        return representation == Representation::Integer || representation == Representation::Bool
               || representation == Representation::Pointer;
    }
    /// Values held as bytes rather than numbers.
    [[nodiscard]] bool isBytes() const {
        return representation == Representation::Record || representation == Representation::VaList
               || representation == Representation::Complex;
    }
};

/// The type without typedef and typeof sugar (its qualifiers are dropped).
[[nodiscard]] const ast::Type* desugared(ast::QualType type);

class TypeTable {
public:
    explicit TypeTable(const TargetInfo& target);

    /// Inline for builtin types, which most operations have: the evaluator asks for every operand.
    [[nodiscard]] TypeInfo describe(ast::QualType type) const {
        if (type->kind() == ast::TypeKind::Builtin) {
            return builtins_[static_cast<std::size_t>(
                static_cast<const ast::BuiltinType*>(type.type())->builtinKind())];
        }
        return describeOther(type);
    }

    // Parts of types; each requires the matching kind.
    [[nodiscard]] static ast::QualType pointee(ast::QualType pointerType);
    [[nodiscard]] static const ast::ArrayType* array(ast::QualType arrayType);
    [[nodiscard]] static const ast::RecordDecl* record(ast::QualType recordType);
    /// The function type of a function or of a pointer to a function; null for other types.
    [[nodiscard]] static const ast::FunctionType* function(ast::QualType type);

    [[nodiscard]] const TargetInfo& target() const { return target_; }
    [[nodiscard]] value::IntegerFormat pointerFormat() const { return pointer_.integer; }
    [[nodiscard]] value::IntegerFormat intFormat() const { return {.bits = 32, .isSigned = true}; }
    /// The format of ptrdiff_t and of size_t.
    [[nodiscard]] value::IntegerFormat ptrdiffFormat() const {
        return {.bits = pointer_.integer.bits, .isSigned = true};
    }
    [[nodiscard]] value::IntegerFormat sizeFormat() const { return pointer_.integer; }

private:
    static constexpr std::size_t builtinCount = static_cast<std::size_t>(ast::BuiltinKind::VaList) + 1;

    [[nodiscard]] TypeInfo describeOther(ast::QualType type) const;

    const TargetInfo& target_;
    std::array<TypeInfo, builtinCount> builtins_{};
    TypeInfo pointer_;
};

} // namespace nitwit::interp
