# Module `sema`

Semantic analysis. It turns the parser's syntactic AST into the typed AST that the interpreter executes (see
`ast/README.md`, "Life cycle of a tree").

- `sema.hpp`: `analyze`.
- `types.hpp`: type queries, value formats, `sizeOf`/`alignOf`, record layouts.
- `constant.hpp`: integer constant expressions.

## What `analyze` does

Declarations are processed in source order, function bodies once their declaration is complete.

- **Types of declarations:**
  - resolve array sizes (constant, or VLA in block scope) and `typeof`;
  - complete arrays from their initializer (`int a[] = {1, 2}`, `char s[] = "ab"`);
  - merge redeclarations into the composite type (C17 6.2.7);
  - enumeration values and the enumeration's integer type, chosen as GCC does;
  - bit-field widths;
  - attributes: `aligned`, `packed`, `mode` (QI, HI, SI, DI, TI, word, pointer), `noreturn`, `transparent_union`
    (ignored), `vector_size` (rejected); other attributes are ignored silently.
- **Record layouts:** the System V ABI as GCC implements it, for i386 and x86-64. That covers the GCC bit-field
  allocation rules, `packed`, `aligned`, zero-length arrays, flexible array members, and unions.
- **Expressions:** C17 6.5 in full.
  - Integer promotions and the usual arithmetic conversions.
  - The type of each integer literal (C17 6.4.4.1p5 with GCC's handling of values above `long long`).
  - Pointer arithmetic, pointer comparisons, the conditional operator with mixed operand types.
  - Assignment conversions, also integer/pointer without a cast, with a warning as in GCC.
  - Default argument promotions for calls without a prototype and for variadic arguments.
  - Lvalue, array and function conversions; member lookup through anonymous members.
  - `sizeof`/`_Alignof`/`__alignof__`, `__builtin_offsetof`, statement expressions, `_Generic`,
    `__builtin_choose_expr`, `__builtin_types_compatible_p`.
- **Initializers:** semantic `InitListExpr`s, with designators and brace elision resolved (C17 6.7.9).
- **Statements:**
  - conditions converted to scalar types;
  - `switch` conditions promoted, case values converted (duplicates are an error);
  - return values converted.
- **Functions:** `setLocals` (the parameters, then every automatic variable in declaration order) and each local's
  `setLocalIndex`.
- **Builtins:** calls to `__builtin_*` functions get their GCC signatures (`__builtin_expect`, `__builtin_memcpy`, ...).
  Implicit declarations of C library functions GCC knows (`malloc`, `memcpy`, `printf`, `abort`, ...) get the library
  signature, as GCC does after warning "incompatible implicit declaration of built-in function".

## Tests

- `tests/unit/sema/`: one test per rule above, including the error and warning cases.
- `tests/golden/sema/`: `<name>.c` and the expected typed dump `<name>.sema`.
- Layout and constant probes: for generated record types and constant expressions (including the corpus's records),
  generate C programs that print `sizeof`, `_Alignof`, `offsetof` and the value of each constant. Compile them with
  GCC in `-m32` and `-m64` and compare with sema's answers.
- `tests/corpus/sema/` (label `corpus`): analyzes every corpus program that parses, and groups the failures by message.
