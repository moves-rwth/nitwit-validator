# Module `sema`

Semantic analysis. It turns the parser's syntactic AST into the typed AST that the interpreter executes (see
`ast/README.md`, "Life cycle of a tree").

- `sema.hpp`: `analyze`.
- `types.hpp`: type queries, value formats, `sizeOf`/`alignOf`, record layouts.
- `constant.hpp`: integer constant expressions.
- `expression.hpp`: `analyzeConstraint`, for witness constraints parsed after the analysis: they are typed like any
  expression and converted to a scalar rvalue like a controlling expression; `\result` has the (unqualified) return
  type of the observed call.

The implementation is split by area: `sema.cpp` (entry point, end of the translation unit), `declarations.cpp`,
`records.cpp` (members, bit-field widths, enumerations), `layout.cpp`, `expressions.cpp`, `operators.cpp`,
`conversions.cpp`, `initializers.cpp`, `statements.cpp`, `constant.cpp`, `builtins.cpp` (the builtin table) and
`types.cpp` with the type rules of `type_rules.hpp`. `sema_impl.hpp` declares the analyzer class they share.

## What `analyze` does

Declarations are processed in source order, function bodies once their declaration is complete. The first error stops
the analysis (`analyze` returns false and the tree must not be used); what GCC accepts with a warning by default in
gnu17, or only with `-fpermissive` in GCC 14 (integer/pointer conversions, incompatible pointer types, implicit
declarations, `return` with or without a value), is accepted with a warning.

- **Types of declarations:**
  - resolve array sizes (constant, or VLA in block and prototype scope) and `typeof`; the size expression of a VLA is
    replaced by its promoted rvalue, which the interpreter evaluates (also for VLA typedefs, at the typedef);
  - parameters written with `typeof` of an array or function type are adjusted to pointers (C17 6.7.6.3p7-8), and the
    function type is rebuilt; a variable of function type (`typeof(*fp) g;`) is rejected as unsupported;
  - complete arrays from their initializer (`int a[] = {1, 2}`, `char s[] = "ab"`); at the end of the translation
    unit, a tentative definition of an array of unknown size gets one element, as in GCC;
  - merge redeclarations into the composite type (C17 6.2.7): each declaration gets the composite of itself and the
    previous ones, so the **last** declaration of an object or function has the complete type. A declaration that
    conflicts with an implicit function declaration replaces its type with a warning. An old-style definition after
    a prototype (`int f(char); int f(c) char c; {}`) keeps the prototype if each parameter's declared or promoted
    type is compatible with the prototype's, as GCC accepts it; otherwise "argument 'c' doesn't match prototype";
  - enumeration values and the enumeration's integer type as GCC chooses it: `unsigned int` if no value is negative,
    else `int`; the narrowest type for `packed` or values outside 32 bits (up to `__int128` on x86-64). If a value
    does not fit into int, all constants of the enumeration have the enumerated type (GCC 14, C23 rules). A constant
    without initializer is the previous one plus one in the previous constant's type, or in `long long`/`unsigned
    long long` if that overflows (`enum { A = 0x7fffffff, B }` is valid);
  - bit-field widths;
  - attributes: `aligned` (records, members, variables, typedefs, `_Alignas`; several give the strictest, zero has no
    effect), `packed`, `mode` (QI, HI, SI, DI, TI, word, pointer, byte; integer types only; on a parameter it also
    changes the prototype), `noreturn`, `weak`, `transparent_union` (on the union or a typedef of it), `alias`
    (accepted; the interpreter resolves it from the attribute). `cleanup`, `vector_size` and `ifunc` are rejected;
    other attributes are ignored silently. Only the argument of `aligned` is analyzed (typed); the arguments of the
    other attributes stay as the parser built them. As in GCC, a struct, union or enum specifier that does not
    define the type ignores the attributes after its keyword and passes those after its tag to the declaration
    (`struct S __attribute__((aligned(64))) member;` aligns the member); the parser implements this rule.
- **Record layouts:** the System V ABI as GCC implements it, for i386 and x86-64 (`layout.cpp` describes the rules):
  GCC's bit-field allocation (no straddling of more units of the type's alignment than the type has; zero-width and
  unnamed bit-fields; `long long` bit-fields in 4-byte units on i386), `packed`, `aligned`, `#pragma pack`
  (`RecordDecl::maxFieldAlignment`), zero-length arrays, flexible array members and unions. `tools/layout-probe`
  compares them with GCC.
- **Expressions:** C17 6.5 in full.
  - Integer promotions and the usual arithmetic conversions. Enumerations promote through their integer type.
    Bit-fields promote as in GCC: narrower than int to `int`, exactly 32 bits to `int`/`unsigned int` by
    signedness; wider ones keep their declared type (GCC computes in an integer type of the bit-field's width).
    This holds through parentheses, comma, assignments, increments and statement expressions (`({ s.u3; }) - 1` is
    an int).
  - The type of each integer literal (C17 6.4.4.1p5; a decimal constant that fits no signed type is `unsigned long`
    or `unsigned long long`, as in GCC). Wide characters and strings have `wchar_t`: `int` on x86-64, `long` on
    i386 (GCC's `__WCHAR_TYPE__`).
  - Pointer arithmetic (GNU: also on `void *` and function pointers), pointer comparisons, the conditional operator
    with mixed operand types.
  - Assignment conversions, also integer/pointer without a cast, with a warning as in GCC.
  - Default argument promotions for calls without a prototype and for variadic arguments.
  - Lvalue, array and function conversions; member lookup through anonymous members.
  - `sizeof`/`_Alignof`/`__alignof__` (of an expression: the alignment of the declaration or member, as GCC; the
    requested alignment of a variable, also a smaller one), `__builtin_offsetof`, statement expressions (whose
    value is that of their last statement, also under labels), `_Generic`, `__builtin_choose_expr`,
    `__builtin_types_compatible_p`.
  - `_Complex` types can be declared, copied and passed; `__real__`/`__imag__` are typed; arithmetic on complex
    values and conversions to or from them are rejected.
- **Initializers:** semantic `InitListExpr`s, with designators and brace elision resolved (C17 6.7.9), see
  `ast/expr.hpp`. A designator continues in the innermost aggregate it entered (p17); unnamed bit-fields get an
  `ImplicitZeroExpr`; anonymous members take part in positional initialization, as in GCC. The gaps of an array
  share one `ImplicitZeroExpr`; an initializer that reaches beyond 2^20 elements is rejected before any is made.
  Only an object with static storage may initialize a flexible array member (GCC).
- **Statements:**
  - conditions converted to scalar rvalues;
  - `switch` conditions promoted, case values converted (duplicates and overlapping ranges are an error);
  - return values converted.
- **Functions:** `setLocals` (the parameters, then every automatic variable in declaration order) and each local's
  `setLocalIndex`. Static and `extern` block-scope variables are not locals; compound literals are not either.
- **Builtins:** `builtins.cpp` has one table of GCC builtins and C library functions with their signatures, written
  in C (`sema::builtinFunctionNames` lists them for the interpreter's library test). Calls to `__builtin_*`
  functions get their GCC signatures; an unknown `__builtin_*` name is an error, as in GCC 14 ("implicit declaration
  of function"). The table includes the checking functions of `_FORTIFY_SOURCE` (`__builtin___memcpy_chk`, ...,
  `__builtin___vsnprintf_chk`). Implicit declarations of C library functions GCC knows (`malloc`, `memcpy`,
  `printf`, `abort`, ...) get the library signature, as GCC does after warning "incompatible implicit declaration
  of built-in function"; explicit declarations keep their own type. Type-generic builtins are checked at each call:
  - `__builtin_add_overflow` and friends, `__builtin_constant_p` (1 for constants and string literals),
    `__builtin_va_start`/`_end`/`_copy`;
  - `__builtin_isnan` and friends take any real floating type unpromoted, `__builtin_fpclassify` its last
    argument; `__builtin_isgreater` and friends need one real floating operand and one of real floating or integer
    type;
  - the atomic builtins `__atomic_*` and `__sync_*` (GCC's `resolve_overloaded_builtin`): T is the unqualified type
    the first argument points to; the `_n` forms and the arithmetic need an integer or pointer T of 1, 2, 4, 8 or 16
    bytes (arithmetic not on `_Bool`), the generic forms (`__atomic_load(p, &r, order)`) any complete object type
    and pointers to objects of T's size. Value arguments are converted to T as by a cast (for arithmetic on a
    pointer to the unsigned integer type of its size: GCC adds bytes), memory orders to int, `weak` to `_Bool`. The
    result is T, `void`, or `_Bool` for the compare-and-exchange builtins. `__atomic_thread_fence`,
    `__atomic_test_and_set`, `__atomic_clear`, `__atomic_always_lock_free`, `__atomic_is_lock_free` and
    `__sync_synchronize` have plain signatures.

## What the interpreter needs to know

- **Function designators are lvalues** (as in Clang); `FunctionToPointerDecay` takes their address, like
  `ArrayToPointerDecay` for arrays.
- **Expressions in void context are not converted:** an expression statement, the left operand of a comma, and
  `(void)e` keep lvalues as lvalues. `return e;` in a void function becomes a `ToVoid` cast.
- **Increments and decrements** have no computation type in the AST: the operand stays an lvalue; the interpreter
  adds 1 in the promoted type of the operand (by the pointee size for pointers, in the type for floating types) and
  converts back, as for `+= 1`.
- **GNU `c ?: b`:** the condition is converted to the result type; the interpreter tests the converted value and
  uses it as the result.
- **va_list** (`__builtin_va_list`) is an opaque object type with the target's size and alignment (i386: 4/4,
  x86-64: 24/8), in both data models; passing a va_list passes a copy. `__builtin_va_start`, `__builtin_va_end` and
  the destination of `__builtin_va_copy` keep their va_list argument as an lvalue. On x86-64, where GCC's va_list is an
  array, CIL-generated programs pass `(__va_list_tag *)&ap` or `&ap` instead; such an argument is a pointer to the
  va_list (an rvalue), and where a va_list value is expected sema inserts the dereference.
- **`__builtin_constant_p(e)`** keeps `e` unconverted and unevaluated; it is 1 exactly if `e` is a string literal
  or `evaluateIntegerConstant` (or the floating evaluation behind it) succeeds.
- **`__builtin_object_size(p, type)`** and `__builtin_dynamic_object_size`: GCC knows the size even at -O0 when
  `p` is the address of a declared object or of a member or element of one (`buf`, `s.a`, `&s.b`, `buf + 4`);
  `sema::knownObjectSize` computes that answer. Otherwise GCC answers `(size_t)-1` for types 0 and 1 and 0 for 2
  and 3.
- **Pointers converted to a wider integer type** sign-extend, as in GCC ("Arrays and pointers" in the GCC manual):
  `(long long)(char *)0x80000000` on i386 is negative. sema's constant evaluation does the same.
- **Compound literals of scalar type** keep their `InitListExpr`, holding the converted value or nothing (zero).
- **Strings in initializers** keep their own type (`char [N+1]`); the object's type says how many bytes to copy and
  to zero.
- **Flexible array members** initialized statically (GNU) get an `InitListExpr` of type `T[n]`; the object is larger
  than `sizeof` of its type.
- **Redeclarations:** all declarations of an object share storage; the last one has the composite type.
- **K&R definitions** have a type without prototype: arguments arrive with the default argument promotions and are
  converted to the parameter types on entry (C17 6.9.1p10). An old-style definition after a prototype has the
  prototype's type, so calls convert their arguments to its parameter types; the parameters keep their declared
  types (`char c` for the prototype's `int`), to which the arguments are converted on entry as well.
- **Calls through an implicit declaration** (or any declaration without a prototype) of a function defined with a
  prototype pass the promoted arguments; the callee's parameters have their declared types. GCC passes the
  promoted value and the callee reads its own type, so the call is undefined (C17 6.5.2.2p6) unless each promoted
  argument type is compatible with the promoted parameter type; then the value is converted to the parameter type.
- **Variable length array parameters:** their size expressions (in the parameter's adjusted pointer type, e.g. the
  `n` of `int (*a)[n]`) are evaluated on function entry, in parameter order, after the arguments are stored.
- **Arrays in rvalue structures:** `f().a[1]` and `(c ? s : t).a` decay an array member of a structure that is not
  an object. The interpreter keeps the structure in a temporary object for the duration of the full expression
  (C17 6.2.4p8), which the pointer of the decay points into.
- **Casts to unions** (GNU) are `ToUnion` casts whose operand has the type of the member it initializes: the first
  member whose type is compatible with the operand's (a scalar or a record of another type). An argument for a
  transparent-union parameter is one too; sema converts it to the member's type first (e.g. `NullToPointer` for `0`).
- **Statement expressions:** the value is that of `StatementExpr::valueStatement()`, the last statement of the body
  if it is an expression statement, also under labels.
- **Atomic builtins** (`__atomic_*`, `__sync_*`) are the evaluator's, as plain single-threaded operations on memory;
  memory orders and fences have no effect. The call's type and the converted arguments are as described under
  Builtins; the callee is the implicit declaration `int ()`, so the evaluator dispatches on its name. Arithmetic on
  a pointer object adds the (unsigned, pointer-sized) value in bytes. `__atomic_compare_exchange(_n)` compares the
  object with `*expected`: if equal, it stores the desired value and returns 1, else it stores the object's value
  into `*expected` and returns 0. `__sync_val_compare_and_swap` returns the old value, `__sync_lock_test_and_set`
  exchanges, `__sync_lock_release` stores 0, `__atomic_test_and_set` sets a byte to 1 and returns whether it was
  set, `__atomic_clear` stores a zero byte.
- **Bit-fields:** a `MemberExpr` of a bit-field has the declared type of the field; reads and writes use the field's
  width and the layout's bit offset.
- **`__builtin_offsetof`:** `Component::field` is the named field, possibly inside anonymous members of the current
  record; `layout.cpp`'s offsets of the anonymous members have to be added (sema's constant evaluation does this).
- Implicit `MemberExpr`s for anonymous members have an empty member name.

## Robustness

The analysis recurses over the tree. Left-nested operator chains (`a + b + ... + z`), `else if` chains and chains of
labels are walked in loops, also by the constant evaluator; other deep trees (postfix chains such as
`p->next->next->...`, right-nested conditionals) recurse, so sema needs a large stack like the parser
(`runWithLargeStack`).

## Known gaps

- `_Float128`/`__float128` are `long double` (the parser warns), so types built from them (glibc's `max_align_t` on
  i386) differ from GCC.
- Bit-fields wider than 32 bits are computed in their declared type; GCC computes in a type of the bit-field's width,
  so `x.b40 + 1` wraps at 2^40 in GCC.
- GCC folds signed overflow, out-of-range floating conversions and the comma operator in some constant contexts
  with a warning; sema treats them as not constant (C17 6.6p4). `const` variables are not constant either.
- Initializers of objects with static storage are not checked for constness; the interpreter evaluates them.
- The `typeof` parameter adjustment covers the parameters of declared functions, not function types nested in
  pointer types.
- An enumeration defined inside a record type that is only used behind pointers in a type name is completed only when
  the record is; using its constants before is reported as an error.
- The parser warns about the implicit declaration of `__atomic_*` and `__sync_*` functions, which GCC knows without
  one. `aligned(0)` is ignored without GCC's warning (the parser turns `_Alignas(0)`, which GCC accepts silently, into
  the same attribute).

## Tests

- `tests/unit/sema/`: one test per rule above, including the error and warning cases.
- `tests/unit/sema/data/zoo_*.c`: a zoo of static assertions about the types and values of expressions
  (`__builtin_types_compatible_p(__typeof__(e), T)`, `e == v`), common to both data models or specific to one.
  `zoo_test.cpp` analyzes them; the CTests `gcc_zoo_*` compile them with `gcc-14 -m32`/`-m64` where available, so
  that GCC confirms every assertion.
- `tests/golden/sema/`: `<name>.c` and the expected typed dump `<name>.sema` (`nitwit-dump --sema`, x86-64), which
  also lists the locals of each function; `tests/golden/sema/ilp32/` the same for i386 (`-32`). The dump shows the
  computation type of compound assignments, the converted size expressions of variable length arrays, the members
  and converted indices of `__builtin_offsetof` and the selection of `_Generic` and `__builtin_choose_expr`.
- `tools/layout-probe/`: for handwritten type zoos (`zoo/`) and corpus programs, generates a probe program that
  compares sema's sizes, alignments, member offsets, bit-field positions, enumeration values and object sizes with
  GCC's (`-m32` and `-m64`).
- `tests/corpus/sema/` (label `corpus`): analyzes every corpus program that parses, with its data model, and groups
  the failures by message; it also counts the warnings of the analyzed programs and lists the most frequent ones.
