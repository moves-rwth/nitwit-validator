# Module `parse`

A recursive-descent parser from tokens (`lex`) to the syntactic AST (`ast`). Besides syntax, it does all name
binding, because C's scoping is purely lexical and the parser has to track typedef names anyway.

## Requirements

- **C17 grammar:**
  - declarations with all specifiers and declarators: abstract declarators, function pointers, arrays of pointers,
    `static`/qualifiers in array parameters, `[*]`;
  - `_Static_assert`, `_Alignas`, `_Generic`, `_Atomic(T)` and `_Atomic`, `_Noreturn`, `inline`;
  - compound literals, designated initializers;
  - `sizeof`/`_Alignof` of types and expressions;
  - all statements.
- **Pre-C99 code from older sv-benchmarks:**
  - implicit `int` (`static x;`, `main() { ... }`);
  - K&R function definitions with identifier lists and parameter declarations;
  - calls of undeclared functions, which create an implicit declaration `int f()` with a warning
    (`FunctionDecl::setImplicit`). It is added to `TranslationUnit::decls` before the external declaration that
    contains the call.
- **GNU extensions:**
  - `__attribute__((...))` wherever GCC accepts it: after specifiers, after declarators, after `struct`/`union`/`enum`,
    on members, on labels and as a statement (`__attribute__((fallthrough));` becomes a NullStmt);
  - `__extension__`, asm labels on declarations (ignored), `asm` statements (only the template is kept);
  - statement expressions, `typeof`/`__typeof__`, `__auto_type`;
  - `?:` without middle operand, case ranges, range designators `[a ... b]`, the obsolete designator `field: value`;
  - zero-length arrays, empty structs;
  - `__builtin_va_arg`, `__builtin_offsetof`, `__builtin_types_compatible_p`, `__builtin_choose_expr`;
  - `__real__`/`__imag__`, `__label__`;
  - `__func__`, `__FUNCTION__`, `__PRETTY_FUNCTION__` as string literals holding the function name.
- **Not supported:** computed goto (`&&label`, `goto *p`), nested functions, `__int128` literals. The parser reports
  an error for them.

## Name binding

- **Ordinary identifiers:**
  - Scopes follow C17 6.2.1: file, block, function prototype, and function scope for labels.
  - Each use of an ordinary identifier binds to the visible VarDecl, FunctionDecl or EnumConstantDecl, and typedef
    names to their TypedefDecl.
  - A redeclaration in the same scope, or an `extern` declaration that refers to an existing file-scope entity, is
    linked through `setPrevious`.
- **Tags:** follow C17 6.7.2.3. `struct S;` declares a new tag in the current scope. `struct S { ... }` completes the
  visible tag of the current scope, or declares a new one. A use like `struct S *p` refers to the visible tag, or
  declares a new one in the current scope. There is exactly one RecordDecl or EnumDecl per tag entity.
- **Labels** bind at the end of the function body: `GotoStmt::setTarget`. Every `switch` gets its case and default
  labels through `setLabels`.
- **Predeclared typedef names:** `__builtin_va_list` (BuiltinKind::VaList), `__int128_t`, `__uint128_t`. The keywords
  `_Float128`, `__float128` and `_Float64x` become `long double` with a warning; `_Float32` becomes `float`,
  `_Float64` and `_Float32x` become `double`.
- **Constraint expressions:** stage 6 will need to parse a witness constraint as if it were written at a given program
  location. A block's declarations are its DeclarationStmts and the parameters are on the FunctionDecl, so the scope
  at a location can be rebuilt from the AST. Do not add separate scope tables to the AST.

## Robustness and performance

- Binary operators use precedence climbing, so recursion depth grows with nesting, not with precedence levels.
- Nesting deeper than 10,000 (parentheses, blocks, declarators) is reported as an error instead of exhausting the
  stack.
- One pass over the token vector, with bounded lookahead. The largest corpus file has 48 MB.

## Tools and tests

- `tools/nitwit-dump/`: a small executable, `nitwit-dump [--tokens|--ast] [-32|-64] <file>`, for debugging and golden
  tests.
- `tests/unit/ast/` and `tests/unit/parse/`: type creation and uniquing; the declarator forms; every statement and
  expression form; each GNU extension; typedef/identifier ambiguities (`T * x;` with T a typedef or a variable,
  `(T)(x)`, `sizeof(T)`, shadowing in inner blocks, parameters shadowing typedefs); tag rules; label resolution;
  implicit declarations and implicit int; K&R definitions; error messages and locations for malformed input.
- `tests/golden/parse/`: `<name>.c` with the expected `<name>.ast` (dump without sema). CTest compares the dumps; with
  `NITWIT_UPDATE_GOLDEN=1` it rewrites them.
- `tests/corpus/parse/` (label `corpus`): parses every corpus program that needs no preprocessing. For each failure it
  reports whether `gcc -fsyntax-only` accepts the file, and it groups failures by message.
