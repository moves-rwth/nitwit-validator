# Module `interp`

Executes the typed AST: memory model, lowering of function bodies, expression evaluation, library functions.

- `memory.hpp`: `Memory`, the flat virtual address space with an allocation table, initialization tracking and
  origins of nondeterministic values.
- `interpreter.hpp`: `buildProgram` (lowering, done once per program) and `Interpreter` (one run).
- `oracle.hpp`: `InputOracle`, the source of nondeterministic values.
- `observer.hpp`: `ExecutionObserver`, the events the validation layer follows, and `EvaluationContext`.
- `library.hpp`: the C library functions, GCC builtins and library objects the interpreter implements, behind
  `LibraryContext`.

## Library: what to implement

The functions sv-benchmarks programs call, and the glibc internals that header macros in preprocessed `.i` files expand
to:

- **SV-COMP:** `__VERIFIER_assume` (handled by the evaluator, because it ends the run), `__VERIFIER_error`,
  `__assert_fail`, `__assert`, `abort`, `exit`, `_Exit`. `__VERIFIER_atomic_begin` and `__VERIFIER_atomic_end` are
  no-ops in a single-threaded run.
- **Memory:** `malloc`, `calloc`, `realloc`, `free`, `memcpy`, `memmove`, `memset`, `memcmp`, `memchr`, and their
  `__builtin_` forms. `alloca` and `__builtin_alloca` are the evaluator's (see below).
- **Strings:** `strlen`, `strnlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `strchr`,
  `strrchr`, `strstr`, `strdup`, `strndup`, `strspn`, `strcspn`.
- **Conversions:** `atoi`, `atol`, `strtol`, `strtoul`, `strtoll`, `strtoull`, `abs`, `labs`, `llabs`.
- **ctype:** `isdigit`, `isalpha`, `isspace`, `toupper`, `tolower`, ..., and glibc's `__ctype_b_loc`,
  `__ctype_tolower_loc`, `__ctype_toupper_loc` with the tables of the C locale.
- **errno:** `__errno_location`.
- **Output:**
  - `printf`, `vprintf`, `sprintf`, `snprintf`, `vsnprintf`;
  - `puts`, `putchar`, `fputs`, `fputc`, `putc`, `fprintf`, `fflush` on `stdout` and `stderr`.
  - The library objects `stdout`, `stderr` and `stdin` are distinct non-null pointers.
  - Formatting follows glibc, so that output is byte-identical to native runs.
- **Math:**
  - The functions of `math.h` that occur in the corpus (`fabs`, `sqrt`, `floor`, `ceil`, `round`, `trunc`, `fmod`,
    `fmin`, `fmax`, `copysign`, `nextafter`, `exp`, `log`, `pow`, `sin`, `cos`, ..., with their `f` and `l` variants),
    computed with the host libm, which is glibc's.
  - glibc's `__fpclassify`, `__isnan`, `__isinf`, `__signbit`, `__finite` (with their `f`/`l` variants).
  - The `__builtin_` forms: `isnan`, `isinf`, `isfinite`, `signbit`, `fabs`, `inf`, `nan`, `huge_val`.
- **GCC builtins:** `__builtin_expect`, `__builtin_unreachable` (undefined behaviour if reached), `__builtin_trap`
  (abort), `__builtin_bswap16/32/64`, `__builtin_clz/ctz/popcount/parity/ffs` with their `l`/`ll` forms (undefined
  behaviour for `clz`/`ctz` of 0), `__builtin_add/sub/mul_overflow`, `__builtin_constant_p` (0),
  `__builtin_object_size` (-1 or 0 for a pointer to an object GCC does not know; sema folds known objects).
- **Unsupported:** threads (`pthread_*`), `setjmp`/`longjmp`, files, `atexit`, signals. These end the run with
  `Unsupported`.

Behaviour choices of the library:
- **Allocation failure** (memory limit, `calloc` size overflow) ends the run with `OutOfMemory` instead of returning
  NULL, as SV-COMP assumes that allocation succeeds. `realloc` that cannot hold the old and the new block at once moves
  the contents through host memory and frees the old block first; the origins of the moved bytes are lost then.
- **Uninitialized bytes** are read as stored by library functions (zero); they do not ask the oracle.
- **Known library functions are never inputs:** every library function in sema's builtin table is implemented or ends
  the run as `Unsupported` (the `scanf` family, files, `qsort`, the `_chk` functions of `_FORTIFY_SOURCE`, ...).
- **GCC builtins that differ from the glibc function of the same name** have their own implementation:
  `__builtin_isinf`/`isinff`/`isinfl` give 1 for -inf (glibc's `isinf` gives -1), `__builtin_isnanl` gives 1 (glibc's
  `isnanl` 65535), `__builtin_isdigit` gives 1 (glibc's `isdigit` the class bit), `__builtin_signbit` gives what
  GCC's inline code gives, and `__builtin_fmin`/`fmax` choose between -0.0 and 0.0 and between two NaNs as GCC's inline
  code does.
- **glibc's behaviour instead of reporting undefined behaviour** where native runs do not fail:
  `printf("%s", NULL)` prints `(null)`; `abs(INT_MIN)` (and `labs`, `llabs`) returns the minimum; `atoi` on overflow
  returns what glibc's `(int) strtol(...)` gives; the ctype functions accept the negative arguments -128..-2 of a
  signed `char` besides EOF (glibc's tables cover them); `memcpy(NULL, NULL, 0)` and other functions with size 0 access
  nothing.
- **Math with the host's 64-bit libm in both data models.** `library_math.cpp` is compiled with `-fno-builtin`, so
  that glibc computes every function, not the compiler's inline version. i386's libm (`-m32 -msse2 -mfpmath=sse`)
  differs in the last bit for many arguments of some functions; share of differing results in the reviewer's
  measurement: `cbrt` 45 %, `lgamma` 37 %, `sinh` 25 %, `atan2f` 17 %, `expm1` 11 %, `tanh`/`tanhf` 11 %, `log1p`
  10 %, `log10f` 8 %, `log10` 6 %, `atanf` 5 %, `tanf` 4 %, `erf` 3 %, `sin`, `cos`, `exp`, `log`, `pow`, `sqrt` at most
  0.3 %; and `fmin(-0.0, 0.0)` gives the other zero. A helper process with the 32-bit libm could remove this
  difference later. Only `lrint` and `lround`, whose result type differs, follow i386 (0x80000000 out of range).
- **Left to the evaluator:**
  - the type-generic builtins whose behaviour depends on argument types: `__builtin_add/sub/mul_overflow` and their
    `_p` forms, `__atomic_*`;
  - the `va_list` of `vprintf` and friends: the evaluator passes the arguments the list refers to;
  - `alloca` and `__builtin_alloca`, whose blocks belong to the stack frame (so `free` of such a block is a free of a
    non-heap pointer, as in glibc).

The library is tested without the evaluator: tests create a `Memory` and a small test context, call the functions,
and compare with values that a natively compiled program prints (glibc on the same machine).

## Replay mode

`nitwit --replay` (see `tools/difftest/README.md`) feeds `__VERIFIER_nondet_*` calls from the inputs file and writes
the input log in the harness's format. Native runs have no counterpart for the other input requests. Uninitialized
reads and calls of unknown functions therefore return 0 in replay mode, and they neither consume input lines nor
appear in the log.

## Memory: behaviour to keep

- **Address layout:** deterministic. Each region gets its own part of the address space, and the null page stays
  unmapped. Allocations are separated by gaps of at least 16 bytes. Stack addresses are reused after `popStack`; heap
  addresses are never reused, so use-after-free is always detected.
- **Lazy commit:** a large global array (sv-benchmarks has arrays of hundreds of MB) costs host memory only for the
  pages that are written. `calloc`-backed buffers achieve that on Linux.
- **Origins:** a sparse map from address ranges to input ids. `write` and `fill` drop origins overlapping the range;
  `copy` moves them along with the bytes.
- **Lookup:** finding the allocation of an address is on the hot path of every load and store. Keep the last
  allocation found as a cache.

The address layout in `memory.cpp`:

| Region | 32-bit pointers | 64-bit pointers |
|---|---|---|
| Code | `0x0001_0000`–`0x0100_0000` | 64 KiB–1 TiB |
| ReadOnly | `0x0100_0000`–`0x0800_0000` | 1–2 TiB |
| Global | `0x0800_0000`–`0x6000_0000` | 2–3 TiB |
| Heap | `0x6000_0000`–`0xC000_0000` | 3–4 TiB |
| Stack | `0xC000_0000`–`0xFFFF_0000`, growing down | 4–5 TiB, growing down |

Accesses below 64 KiB are null-pointer accesses. Stack addresses are reused because the stack region is bounded (1 GiB
with 32-bit pointers): a program that calls functions in a long loop would exhaust it otherwise. The price is that a
use of a dangling pointer to a local is detected as DeadObject only until a later stack allocation reuses its place;
after that, the access reaches the new object, as in a native run.

## Execution: behaviour to keep

- **Lowering:** each function body becomes a flat instruction list: evaluate, declare, branch, jump, switch, return,
  end scope. So `goto`, `switch` into nested blocks, `break` and `continue` are plain jumps. Expressions stay trees.
- **Events:** each instruction knows the statement and locations it comes from, so the observer gets exactly the
  events of `observer.hpp`. In particular:
  - a `ConditionEvent` for every evaluation of a controlling expression, including `for` loops without a condition
    (outcome true);
  - one `CallEvent` and one `ReturnEvent` per call, including library functions and nondet functions.
- **Objects:**
  - Globals and string literals are allocated and initialized before `main`, in declaration order. Global
    initializers are evaluated by the evaluator itself; C guarantees that they are constant.
  - Automatic variables are allocated at function entry and released at return, except variable length arrays, which
    are allocated at their declaration and released at the end of their scope.
  - A declaration without an initializer leaves its object uninitialized. A read of an uninitialized scalar becomes an
    `UninitializedRead` input request. The value is then written to the object, so later reads agree.
- **Origins:** an origin means that a value is derived from that input only through copies and conversions. A value
  read from memory carries the origin stored for exactly its range. Copies keep the origin (assignment, argument
  passing, return), and so do conversions between arithmetic types, also those that change the value (`char c = x;`,
  `float f = d;`, `(int)d`): the validation layer chooses inputs through them and confirms only by a complete run.
  Every operation (arithmetic, comparison, logical) drops it.
  - Known imprecision: memory keeps origins by address and size, not by type. A read of the same bytes through
    another type of the same size (a union member, a cast pointer) keeps the origin, although its value reinterprets
    the input's bytes instead of converting them. A value chosen through such a read may then not give the value the
    validation wanted; soundness is unaffected, since the next run is checked like any other.
- **Calls:**
  - Through function designators and function pointers. Variadic functions, with `va_start`/`va_arg`/`va_end`/
    `va_copy` over the interpreter's own argument list.
  - Struct arguments and struct return values by value.
  - Recursion depth is limited by `RunOptions::maxCallDepth`.
- **Library functions:** a function without a definition is first looked up in the interpreter's library:
  - SV-COMP: `__VERIFIER_nondet_*`, `__VERIFIER_assume`, `__VERIFIER_error`, `__assert_fail`, `abort`, `exit`,
    `_Exit`;
  - memory: `malloc`, `calloc`, `realloc`, `free`, `alloca`;
  - strings: `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcpy`, `strncpy`, `strcmp`, `strncmp`, `strcat`,
    `strchr`, `strrchr`, `strdup`;
  - output: `printf`, `fprintf` to stdout/stderr, `puts`, `putchar`, `sprintf`, `snprintf`;
  - math (`fabs`, `sqrt`, `floor`, `ceil`, `fmod`, the `isnan`/`isinf` family, ...);
  - GCC builtins such as `__builtin_expect`, `__builtin_unreachable`, `__builtin_bswap*`, `__builtin_clz/ctz/popcount`,
    `__builtin_*_overflow`, `__builtin_memcpy`.

  A function found neither as a definition nor in the library returns a value from the oracle (`UndefinedFunction`),
  and its arguments have no effect.
- **Undefined behaviour:**
  - Operations without a defined result end the run with `Termination::UndefinedBehavior` and a message: memory
    errors, integer division by zero, a call through a pointer to no function, a non-void function falling off its
    end whose value is used.
  - Signed overflow and invalid shifts are reported through `onUndefinedBehavior` and continue with the hardware
    result.

## Evaluator: how it is built, and its behaviour choices

The implementation is split by area: `lowering.cpp` (`buildProgram`, `program.hpp`), `execution.cpp` (start-up,
ending, limits), `statements.cpp` (the instruction loop), `calls.cpp`, `expressions.cpp`, `lvalues.cpp`,
`conversions.cpp`, `initialization.cpp`, `builtins.cpp` and `events.cpp`; `execution.hpp` declares the class
`Execution` they share, `type_info.hpp` the representation of types (computed without canonicalizing, because the
evaluator asks for it at every operation), and `host_stack.hpp` the check of the host stack.

- **Entities:** functions and objects with linkage are identified by name, so all their declarations (also in blocks)
  share one entity; block-scope statics are entities of their own. The last declaration gives the type. GNU
  `alias("target")` makes a function or object the target's. A weak function without definition has the address 0.
- **Functions without definition**, in this order: the evaluator's builtins (`__VERIFIER_assume`,
  `__VERIFIER_nondet_memory`, `__builtin_va_*`, `__builtin_constant_p`, `__builtin_object_size` (the size
  `sema::knownObjectSize` gives, else the library's answer), `alloca`, the type-generic overflow builtins and their
  `_p` forms, `__atomic_*`, `__sync_*`); `__VERIFIER_nondet_<anything>` (the oracle, `NondetCall`); the library; weak
  functions (calling one is undefined); else the oracle (`UndefinedFunction`) for a scalar result, zero for a struct.
- **Calls through a declaration without prototype** of a function defined with one (C17 6.5.2.2p6) are undefined if
  the numbers of arguments and parameters differ, or if a promoted argument does not fit its promoted parameter: the
  same integer width (signed and unsigned only with a value both hold), pointer for pointer, the same floating type,
  records of the same size. Otherwise each argument is converted to its parameter's type, as for K&R definitions.
- **Order of evaluation** (unspecified in C, visible in the order of the inputs) as GCC on x86 and x86-64 (GCC ≥ 7,
  `gimplify_modify_expr`): the arguments of a call from right to left; the operands of the other binary operators
  from left to right; for `=` the right operand first, unless it is a call (also under parentheses and conversions
  that keep the representation), then the left operand's address first; for compound assignments the right operand
  first. GCC folds trivial arithmetic first (`arr[i()] = f() + 0` calls i first); the interpreter does not.
- **Steps** (`RunOptions::maxSteps`): statements, controlling expressions and calls, so that loops without statements
  are limited too.
- **Host stack:** calls and expressions recurse on the host stack. `invoke`, `rvalue` and `lvalue` check the frame
  address against the thread's stack bounds (`host_stack.hpp`) and end the run with `LimitReached` ("host stack
  exhausted") when less than 1 MiB is left, before `maxCallDepth` if need be.
- **Events** are reported for steps of the program only: not while static storage is initialized before `main`, and
  not while the observer evaluates a constraint.
- **Storage:**
  - Each local is an allocation of its own (out-of-bounds accesses between locals are detected). Reaching the
    declaration of a local without initializer again (in a loop, after a `goto`) makes it indeterminate again
    (C17 6.2.4p6, `Memory::markUninitialized`), so that the next read is a new input. Compound literals in a function
    get storage at function entry, one object per literal and call, re-initialized at each evaluation.
  - `alloca` blocks are stack allocations of the frame; like GCC, the end of a variable length array's scope also
    ends the blocks allocated after it. Freeing one is undefined.
  - Struct and union rvalues that are not objects (arguments, results, values whose member is addressed as in
    `f().a[1]`) live in heap temporaries of the frame, which end with the full expression, i.e. after the instruction
    that made them (C17 6.2.4p8).
- **Records** keep the initialization state and the origins of their bytes when they are copied: assignment and
  initialization from another record, arguments (copied into a temporary when the argument is evaluated, then into the
  parameter), results (copied into a temporary of the caller before the callee's frame ends), `?:` and the comma
  operator (which select an object). An assignment between partly overlapping objects is undefined
  (C17 6.5.16.1p3). Known limitation: copying an uninitialized record copies its uninitialized state, so reading the
  same member of both copies asks for two independent inputs, while a native run reads equal values.
- **Uninitialized reads:** a scalar with uninitialized bytes is answered by the oracle; bytes that were written keep
  their values (then the value has no origin). Every answer is normalized to the requested type (`_Bool` 0 or 1,
  integers truncated). A bit-field whose storage has uninitialized bytes asks for those bytes (as `unsigned char`
  inputs), stores them, and reads the field from them; neighbouring fields in the same bytes read the same input.
- **Conversions:** a pointer converted to a wider integer is sign-extended (GCC, "Arrays and pointers"). Origins
  survive every conversion between arithmetic types (see "Origins"); conversions to or from pointers keep them only
  where every value is preserved.
- **va_list:** the object holds a handle (a number in its first four bytes) of a cursor over the variadic arguments of
  the frame that called `va_start`. A copy of the object (passing a va_list) shares the cursor, as on x86-64;
  `va_copy` makes an independent one. `va_arg` with a type of another kind or size than the argument's (C17
  7.16.1.1p2; signed and unsigned versions of a type, and integers and pointers of the same width, are accepted),
  after the last argument, or after the function returned is undefined. Library functions with a va_list get the
  remaining arguments instead.
- **Calls through a pointer of another type** (C17 6.5.2.2p9) convert scalar arguments and results like the
  registers that pass them; a struct where a scalar is expected, or the other way round, is undefined.
- **Pass-through builtins** (`__builtin_expect`, `__builtin_expect_with_probability`, `__builtin_assume_aligned`)
  return their first argument converted to the result type, with its origin.
- **Statement expressions** are lowered in place, behind a jump over them, and run as a range of the function's
  instructions; `break`, `continue`, `goto` and `return` out of them abandon the enclosing expression (GNU). Their
  value is that of `StatementExpr::valueStatement()`.
- **Undefined behaviour without a defined result** stops the run with a message naming the operation and, for memory
  errors, the kind and the object (`invalid read of 4 bytes at 0x...: out of bounds ('a')`). Floating to integer
  conversions out of range are reported and continue, like signed overflow.
- **Constraints** (`EvaluationContext::evaluate`): anything with an effect on the program's state is an error
  (assignments, increments, calls, statement expressions, compound literals, temporaries, `va_arg`), and so is any
  undefined behaviour; uninitialized reads ask the oracle. The evaluation reports no events.
- **`sizeof`** of an expression of variable length array type evaluates the expression (C17 6.5.3.4p2).

## Tests

- `tests/unit/interp/memory_test.cpp`: the memory behaviour above:
  - allocation in every region with sizes 0, 1 and large; alignment; the null page; gaps between allocations;
  - accesses inside, across the end and one past the end; dead heap blocks; double free; freeing non-base and
    non-heap addresses; LIFO stack reuse;
  - read-only and code protection; initialization per byte;
  - origins set, read, partly overwritten and copied (overlapping ranges too);
  - deterministic addresses; the limits of the 32-bit address space; a 1 GiB global written at two places only.
- `tests/unit/interp/`: interpreter tests on small programs (source text → output and termination), with a test
  oracle and a recording observer for the event sequences (`interpreter_test_support.hpp`): `lowering_test.cpp`
  (statements, conditions, jumps and calls as events), `evaluator_test.cpp` (operators, conversions, objects and
  calls in both data models, outputs confirmed with GCC), `termination_test.cpp` (how runs end, undefined behaviour,
  limits), `inputs_test.cpp` (oracle requests, uninitialized reads, origins) and `evaluation_context_test.cpp`.
- `tests/semantics/`: the programs of the semantics suite (see `tools/difftest/README.md`) must print the expected
  output under `-32` and `-64` (CTest label `semantics`).
- `tools/difftest/`: Csmith programs and corpus programs with random inputs; the interpreter must agree with the
  native runs.
