#pragma once

// Values the validator chooses for inputs: the conversions between the arithmetic types of a constraint and of an
// input, and values that satisfy a relation `x op constant` (validate/README.md, "Values").

#include <optional>
#include <span>
#include <string>

#include "ast/context.hpp"
#include "ast/type.hpp"
#include "support/int128.hpp"
#include "value/arithmetic.hpp"
#include "value/value.hpp"

namespace nitwit::validate {

/// `op constant` in the type of a comparison, which both operands have after the usual arithmetic conversions.
struct Bound {
    value::Comparison op = value::Comparison::Equal;
    value::Value constant;
    ast::QualType type;
};

/// The relation that holds exactly when `op` does not (C17 6.5.8, 6.5.9); for floating types only if no NaN occurs.
[[nodiscard]] value::Comparison negated(value::Comparison op);

/// `constant op x` as `x op' constant`.
[[nodiscard]] value::Comparison mirrored(value::Comparison op);

/// Integer (including _Bool, char and enumerations) and real floating types: the types of the inputs NITWIT chooses.
[[nodiscard]] bool isArithmetic(ast::AstContext& context, ast::QualType type);

/// `number` as a value of the arithmetic type `type`, converted as C converts an integer constant (C17 6.3.1).
[[nodiscard]] value::Value valueOf(ast::AstContext& context, Int128 number, ast::QualType type);

/// A value of the arithmetic type `from` converted to the arithmetic type `to` (C17 6.3.1); nullopt for other types
/// and for floating values out of the range of an integer type.
[[nodiscard]] std::optional<value::Value> convert(ast::AstContext& context, const value::Value& value,
                                                  ast::QualType from, ast::QualType to);

/// Whether `value op bound.constant` holds in the bound's type.
[[nodiscard]] bool satisfies(ast::AstContext& context, const value::Value& value, const Bound& bound);

/// A scalar value as text, for the log: integers in decimal (signed if `type` is), floating values in the shortest
/// form that reads back exactly, pointers in hexadecimal.
[[nodiscard]] std::string toString(ast::AstContext& context, const value::Value& value, ast::QualType type);

/// A value of the arithmetic type `type` that, converted to each bound's type, satisfies all bounds; nullopt if none
/// of the candidates does. The candidates are, for each bound in turn, the value closest to its constant that
/// satisfies it: the constant itself for ==, <= and >=, its neighbour for < and >, and 0 (or 1 if the constant is 0)
/// for !=.
[[nodiscard]] std::optional<value::Value> choose(ast::AstContext& context, std::span<const Bound> bounds,
                                                 ast::QualType type);

/// The value of an input of type `inputType` that makes a subject of type `subjectType` satisfy all bounds, when the
/// program converts the input to the subject's type: choose() for the subject, if that value comes back from the
/// input's type unchanged.
[[nodiscard]] std::optional<value::Value> chooseInput(ast::AstContext& context, std::span<const Bound> bounds,
                                                      ast::QualType subjectType, ast::QualType inputType);

} // namespace nitwit::validate
