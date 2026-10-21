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
- **Memory:** `malloc`, `calloc`, `realloc`, `free`, `alloca`, `__builtin_alloca`, `memcpy`, `memmove`, `memset`,
  `memcmp`, `memchr`, and their `__builtin_` forms.
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
  `__builtin_object_size` (-1 or 0, as GCC at -O0).
- **Unsupported:** threads (`pthread_*`), `setjmp`/`longjmp`, files, `atexit`, signals. These end the run with
  `Unsupported`.

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
- **Origins:** a value read from memory carries the origin stored for exactly its range. Copies keep the origin
  (assignment, argument passing, return, conversions that preserve the value, such as int to long). Arithmetic drops
  it.
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

## Tests

- `tests/unit/interp/memory_test.cpp`: the memory behaviour above:
  - allocation in every region with sizes 0, 1 and large; alignment; the null page; gaps between allocations;
  - accesses inside, across the end and one past the end; dead heap blocks; double free; freeing non-base and
    non-heap addresses; LIFO stack reuse;
  - read-only and code protection; initialization per byte;
  - origins set, read, partly overwritten and copied (overlapping ranges too);
  - deterministic addresses; the limits of the 32-bit address space; a 1 GiB global written at two places only.
- `tests/unit/interp/`: interpreter tests on small programs (source text → output and termination), with a test
  oracle and a recording observer for the event sequences.
- `tests/semantics/`: the programs of the semantics suite (see `tools/difftest/README.md`) must print the expected
  output under `-32` and `-64` (CTest label `semantics`).
- `tools/difftest/`: Csmith programs and corpus programs with random inputs; the interpreter must agree with the
  native runs.
