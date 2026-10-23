#pragma once

// Where nondeterministic values come from. The interpreter asks the oracle whenever the program reads a value that the
// program itself does not determine. The validation layer answers from the witness, from earlier runs, or with
// defaults; replay mode answers from an input file.

#include <cstdint>

#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "ast/type.hpp"
#include "value/value.hpp"

namespace nitwit::interp {

struct InputRequest {
    enum class Kind : std::uint8_t {
        NondetCall,        // __VERIFIER_nondet_<type>()
        UndefinedFunction, // the return value of a function without definition that the interpreter does not know
        UninitializedRead, // a read of an automatic or heap object that was never written
    };

    Kind kind = Kind::NondetCall;
    value::InputId id{};                         // the position of this request in the run, from 0
    const ast::Expr* site = nullptr;             // the call, or the expression that reads
    std::uint32_t occurrence = 0;                // how many requests this site made before in this run
    ast::QualType type;                          // the type of the requested value (scalar)
    const ast::FunctionDecl* function = nullptr; // the called function, for calls
};

class InputOracle {
public:
    virtual ~InputOracle() = default;

    /// A value in the representation of `request.type` (see sema::integerFormat and sema::floatFormat). The
    /// interpreter registers it as the input `request.id` of its terms (value::TermTable::input).
    virtual value::Value provide(const InputRequest& request) = 0;
};

} // namespace nitwit::interp
