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
  an error for them. As in GCC, a `case` or `default` label inside a statement expression cannot belong to a switch
  outside it ("switch jumps into statement expression"); a goto into a statement expression is not detected.
- **Left to sema:** parameters whose type is written with `typeof` (`void f(typeof(a) p)`) are not adjusted from
  array or function types to pointers, and a declaration whose type is `typeof` of a function other than a plain
  function name (`typeof(*fp) g;`) declares a variable. `_Static_assert` among struct members is parsed and dropped
  (the programs compile, so it holds).

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
- **Function definitions:** the parameter list's scope, with the parameters and any tags and enumeration constants
  declared in it, is the outermost scope of the body (C17 6.2.1p4). The declaration list of a K&R definition is
  parsed in that scope, each parameter in scope after its declarator.
- **Labels** bind at the end of the function body: `GotoStmt::setTarget`. Every `switch` gets its case and default
  labels through `setLabels`.
- **Predeclared typedef names:** `__builtin_va_list` (BuiltinKind::VaList), `__int128_t`, `__uint128_t`. The keywords
  `_Float128` and `__float128` become `long double` with a warning, `_Float64x` becomes `long double` (which it is on
  x86); `_Float32` becomes `float`, `_Float64` and `_Float32x` become `double`.
- **Declaration lists** (`TranslationUnit::decls`, `DeclarationStmt::decls`): a struct, union or enum definition is
  listed once, before the declarators of the declaration that contains it, and nested definitions come before the
  enclosing one. In a block, `struct S;` is listed in its DeclarationStmt, since it hides an outer S; at file scope it
  lists nothing. Definitions inside type names (casts, `sizeof`, compound literals) and parameter lists are only
  reachable through the types, and enumeration constants declared there (`sizeof(enum { A, B })`) are bound but
  listed nowhere. Implicit function declarations precede the external declaration that caused them.
- **Pragmas:** `#pragma pack` follows GCC: `pack()` and `pack(N)` set the maximum field alignment (N = 1, 2, 4, 8, 16;
  0 or none means no limit), `pack(push[, id][, N])` saves it on a stack, `pack(pop[, id])` restores the saved value
  (with an id, the one saved by the matching push). It sets `RecordDecl::maxFieldAlignment` of the records whose
  definitions end while it is in effect. Malformed forms such as `pack(pop, 2)` are ignored with a warning.
  `#pragma weak f` marks every declaration of the function f as weak, wherever f is declared. Other pragmas are
  ignored, wherever they occur.
- **Constraint expressions** (`constraint.hpp`): `parseConstraint` parses a witness constraint as if it were written
  at a program location.
  - **Text:** it is registered as the file `<constraint>`, so diagnostics point into it. `\result` (a backslash
    immediately followed by `result`) becomes a ResultExpr: constraint.cpp turns the two tokens into one identifier
    token named `\result`, which no lookup finds. Any other backslash (`\old`, a stray `\`) and preprocessing
    directives are errors.
  - **Scope:** rebuilt from the AST, which has no scope tables. A block's declarations are its DeclarationStmts and
    the parameters are on the FunctionDecl. The ordinary identifiers, typedef names, tags and enumeration constants
    of these declarations are visible, innermost last:
    - the file-scope declarations of the whole unit, also those after the location: a witness may name a global
      declared later, which is harmless;
    - with a function, its parameters, also at locations outside its body (such as the function's own line);
    - each block of the body that contains the location, with its declarations before the location (C17 6.2.1p4).
      A declarator is in scope after its completion (C17 6.2.1p7): in `int b = 4, c = b, d;` with the location at
      the second `b`, b and c are visible, d is not. A `for` declaration belongs to its loop, a labeled declaration
      to the enclosing block.
    - Not rebuilt: blocks inside statement expressions, and tags and enumeration constants declared in parameter
      lists or type names.
  - **The program stays unchanged:** the rebuilt scopes bind the program's declarations without linking
    redeclarations, and the constraint gets a scope of its own, so that `struct S { ... }` in a cast declares a new
    tag instead of completing one of the program. New nodes go to the AstContext arena; each call uses a new parser,
    so no scope outlives it.
  - **Grammar:** one assignment-expression (C17 6.5.16). A comma or `;` at the top level is an error ("expected end
    of constraint"); a comma in parentheses is the comma operator.
  - **Rejected:** a constraint is evaluated while the program is paused at a waypoint, so it must not change the
    program's state. Assignments, `++` and `--`, function calls (also of builtins), statement expressions and
    `__builtin_va_arg` are errors, also in operands that are not evaluated, such as `sizeof`. None of the 58,119
    witnesses of the SV-COMP 2026 corpus contains any of them. An undeclared identifier is an error also before `(`:
    a constraint declares no function implicitly.
  - **Nesting** is limited to 256 levels instead of 10,000, since constraints are parsed on the caller's stack.

## Robustness and performance

- Binary operators use precedence climbing, so recursion depth grows with nesting, not with precedence levels.
- Nesting deeper than 10,000 (parentheses, blocks, declarators) is reported as an error instead of exhausting the
  stack. The parser needs about 1 KiB of stack per level without optimization, more under ASan, so it must run on a
  large stack (`runWithLargeStack`, at least 256 MiB); it creates no threads itself.
- The depth of the tree is not bounded by that limit: left-associative operator chains, postfix chains (`a[1][2]`,
  `p->next->next`), pointer declarators, `else if` chains and consecutive labels are parsed in loops and can be
  arbitrarily deep. Later stages must not assume a bound.
- One pass over the token vector, with bounded lookahead. The largest corpus file has 48 MB.

## Tools and tests

- `tools/nitwit-dump/`: a small executable, `nitwit-dump [--tokens|--ast] [-32|-64] <file>`, for debugging and golden
  tests.
- `tests/unit/ast/` and `tests/unit/parse/`: type creation and uniquing; the declarator forms; every statement and
  expression form; each GNU extension; typedef/identifier ambiguities (`T * x;` with T a typedef or a variable,
  `(T)(x)`, `sizeof(T)`, shadowing in inner blocks, parameters shadowing typedefs); tag rules; label resolution;
  implicit declarations and implicit int; K&R definitions; error messages and locations for malformed input; witness
  constraints (`constraint_test.cpp`: scopes at locations, `\result`, the rejected forms, and that sema accepts the
  results).
- `tests/golden/parse/`: `<name>.c` with the expected `<name>.ast` (dump without sema). CTest compares the dumps; with
  `NITWIT_UPDATE_GOLDEN=1` it rewrites them.
- `tests/corpus/parse/` (label `corpus`): parses every corpus program that needs no preprocessing. For each failure it
  reports whether `gcc -fsyntax-only` accepts the file, and it groups failures by message.
