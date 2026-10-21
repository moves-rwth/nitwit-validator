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
    conflicts with an implicit function declaration replaces its type with a warning;
  - enumeration values and the enumeration's integer type as GCC chooses it: `unsigned int` if no value is negative,
    else `int`; the narrowest type for `packed` or values outside 32 bits. If a value does not fit into int, all
    constants of the enumeration have the enumerated type (GCC 14, C23 rules);
  - bit-field widths;
  - attributes: `aligned` (records, members, variables, typedefs, `_Alignas`), `packed`, `mode` (QI, HI, SI, DI, TI,
    word, pointer, byte; integer types only), `noreturn`, `weak`, `transparent_union` (on the union or a typedef of
    it), `alias` (accepted; the interpreter resolves it from the attribute). `cleanup`, `vector_size` and `ifunc`
    are rejected; other attributes are ignored silently. As in GCC, a struct, union or enum specifier that does not
    define the type ignores the attributes after its keyword and passes those after its tag to the declaration
    (`struct S __attribute__((aligned(64))) member;` aligns the member); the parser was changed accordingly.
- **Record layouts:** the System V ABI as GCC implements it, for i386 and x86-64 (`layout.cpp` describes the rules):
  GCC's bit-field allocation (no straddling of more units of the type's alignment than the type has; zero-width and
  unnamed bit-fields; `long long` bit-fields in 4-byte units on i386), `packed`, `aligned`, `#pragma pack`
  (`RecordDecl::maxFieldAlignment`), zero-length arrays, flexible array members and unions. `tools/layout-probe`
  compares them with GCC.
- **Expressions:** C17 6.5 in full.
  - Integer promotions and the usual arithmetic conversions. Enumerations promote through their integer type.
    Bit-fields promote as in GCC: narrower than int to `int`, exactly 32 bits to `int`/`unsigned int` by
    signedness; wider ones keep their declared type (GCC computes in an integer type of the bit-field's width).
  - The type of each integer literal (C17 6.4.4.1p5; a decimal constant that fits no signed type is `unsigned long`
    or `unsigned long long`, as in GCC).
  - Pointer arithmetic (GNU: also on `void *` and function pointers), pointer comparisons, the conditional operator
    with mixed operand types.
  - Assignment conversions, also integer/pointer without a cast, with a warning as in GCC.
  - Default argument promotions for calls without a prototype and for variadic arguments.
  - Lvalue, array and function conversions; member lookup through anonymous members.
  - `sizeof`/`_Alignof`/`__alignof__` (of an expression: the alignment of the declaration or member, as GCC),
    `__builtin_offsetof`, statement expressions, `_Generic`, `__builtin_choose_expr`,
    `__builtin_types_compatible_p`.
  - `_Complex` types can be declared, copied and passed; `__real__`/`__imag__` are typed; arithmetic on complex
    values and conversions to or from them are rejected.
- **Initializers:** semantic `InitListExpr`s, with designators and brace elision resolved (C17 6.7.9), see
  `ast/expr.hpp`. A designator continues in the innermost aggregate it entered (p17); unnamed bit-fields get an
  `ImplicitZeroExpr`; anonymous members take part in positional initialization, as in GCC.
- **Statements:**
  - conditions converted to scalar rvalues;
  - `switch` conditions promoted, case values converted (duplicates and overlapping ranges are an error);
  - return values converted.
- **Functions:** `setLocals` (the parameters, then every automatic variable in declaration order) and each local's
  `setLocalIndex`. Static and `extern` block-scope variables are not locals; compound literals are not either.
- **Builtins:** `builtins.cpp` has one table of GCC builtins and C library functions with their signatures, written
  in C. Calls to `__builtin_*` functions get their GCC signatures. Implicit declarations of C library functions GCC
  knows (`malloc`, `memcpy`, `printf`, `abort`, ...) get the library signature, as GCC does after warning
  "incompatible implicit declaration of built-in function"; explicit declarations keep their own type. Type-generic
  builtins (`__builtin_add_overflow`, `__builtin_isnan`, `__builtin_constant_p`, `__builtin_va_start`, ...) are
  checked at each call.

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
- **`__builtin_constant_p(e)`** keeps `e` unconverted and unevaluated; it is 1 exactly if `evaluateIntegerConstant`
  succeeds.
- **Compound literals of scalar type** keep their `InitListExpr`, holding the converted value or nothing (zero).
- **Strings in initializers** keep their own type (`char [N+1]`); the object's type says how many bytes to copy and
  to zero.
- **Flexible array members** initialized statically (GNU) get an `InitListExpr` of type `T[n]`; the object is larger
  than `sizeof` of its type.
- **Redeclarations:** all declarations of an object share storage; the last one has the composite type.
- **K&R definitions** have a type without prototype: arguments arrive with the default argument promotions and are
  converted to the parameter types on entry (C17 6.9.1p10).
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

## Tests

- `tests/unit/sema/`: one test per rule above, including the error and warning cases.
- `tests/golden/sema/`: `<name>.c` and the expected typed dump `<name>.sema` (`nitwit-dump --sema`, x86-64), which
  also lists the locals of each function.
- `tools/layout-probe/`: for handwritten type zoos (`zoo/`) and corpus programs, generates a probe program that
  compares sema's sizes, alignments, member offsets, bit-field positions, enumeration values and object sizes with
  GCC's (`-m32` and `-m64`).
- `tests/corpus/sema/` (label `corpus`): analyzes every corpus program that parses, with its data model, and groups
  the failures by message.
