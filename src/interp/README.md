# Module `interp`

Executes the typed AST: memory model, lowering of function bodies, expression evaluation, library functions.

- `memory.hpp`: `Memory`, the flat virtual address space with an allocation table, initialization tracking and the
  terms of the bytes (see "Terms").
- `interpreter.hpp`: `buildProgram` (lowering, done once per program) and `Interpreter` (one run).
- `oracle.hpp`: `InputOracle`, the source of nondeterministic values.
- `observer.hpp`: `ExecutionObserver`, the events the validation layer follows, and `EvaluationContext`.
- `library.hpp`: the C library functions, GCC builtins and library objects the interpreter implements, behind
  `LibraryContext`.

## Library

The library implements the functions sv-benchmarks programs call, and the glibc internals that header macros in
preprocessed `.i` files expand to:

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
- **Unsupported:** threads (`pthread_*`), GCC's `__builtin_setjmp`/`__builtin_longjmp`, files, `atexit`, signals,
  rounding modes other than to nearest (`fesetround`). These end the run with `Unsupported`. `setjmp` and `longjmp`
  are the evaluator's ("Non-local jumps").

Behaviour choices of the library:
- **Allocation never returns NULL:** SV-COMP assumes that allocation succeeds, and programs that need failing
  allocations model them: the aws harnesses separate `bounded_malloc`, which must succeed, from `can_fail_malloc`,
  which returns NULL by a nondeterministic choice of the program. An allocation the interpreter cannot make (beyond
  the memory limit, a `calloc` whose size overflows) therefore ends the run with `OutOfMemory` (exit 251).
  `realloc` that cannot hold the old and the new block at once moves the contents through host memory and frees the
  old block first; the moved bytes are opaque then if they depend on inputs (see "Terms").
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
  differs in the last bit for many arguments of some functions; measured share of differing results: `cbrt` 45 %,
  `lgamma` 37 %, `sinh` 25 %, `atan2f` 17 %, `expm1` 11 %, `tanh`/`tanhf` 11 %, `log1p` 10 %, `log10f` 8 %, `log10`
  6 %, `atanf` 5 %, `tanf` 4 %, `erf` 3 %, `sin`, `cos`, `exp`, `log`, `pow`, `sqrt` at most 0.3 %; and
  `fmin(-0.0, 0.0)` gives the other zero. A helper process with the 32-bit libm would remove this
  difference (`docs/roadmap.md`). Only `lrint` and `lround`, whose result type differs, follow i386 (0x80000000 out
  of range).
- **Left to the evaluator:**
  - the type-generic builtins whose behaviour depends on argument types: `__builtin_add/sub/mul_overflow` and their
    `_p` forms, `__atomic_*`;
  - the `va_list` of `vprintf` and friends: the evaluator passes the arguments the list refers to;
  - `alloca` and `__builtin_alloca`, whose blocks belong to the stack frame (so `free` of such a block is a free of a
    non-heap pointer, as in glibc);
  - `setjmp` and `longjmp` with glibc's other names, which save and restore the frames ("Non-local jumps").

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
- **Dead heap blocks** stay in the allocation table, but runs of adjacent dead blocks (no live block between them) are
  merged into one dead range once the blocks freed since the last merge make up half of the table, and at the earliest
  after 1024 frees. So the table grows with the live blocks, not with every `free` (or heap temporary): a
  `malloc`/`free` loop would otherwise add 56 bytes per iteration, 1.8 GB after 19 s. Every byte of a freed block stays
  dead, and a double free stays "a heap block that was freed before" (`free` checks for a dead block before it checks
  the base). What merging changes: the gaps between the blocks of a range are dead too (an access there is DeadObject,
  not InvalidAddress), `find` gives the range's base and size, and a range of blocks with different names (`heap`,
  `temporary`) is named `heap`.
- **Lazy commit:** a large global array (sv-benchmarks has arrays of hundreds of MB) costs host memory only for the
  pages that are written. `calloc`-backed buffers achieve that on Linux.
- **Terms:** a sparse map from address ranges to the terms of the scalars written there, or to "opaque" (see "Terms").
  Lookups cost nothing while the map is empty, as in programs without inputs.
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
  - one `CallEvent` and one `ReturnEvent` per call, including library functions and nondet functions; but a function
    that a longjmp leaves does not return, and a setjmp call returns once more after each longjmp to it ("Non-local
    jumps").
- **Objects:**
  - Globals and string literals are allocated and initialized before `main`, in declaration order. Global
    initializers are evaluated by the evaluator itself; C guarantees that they are constant.
  - Automatic variables are allocated at function entry and released at return, except variable length arrays, which
    are allocated at their declaration and released at the end of their scope.
  - A declaration without an initializer leaves its object uninitialized. A read of an uninitialized scalar becomes an
    `UninitializedRead` input request. The value is then written to the object, so later reads agree.
- **Terms:** every value derived from an input carries a term (see "Terms").
- **Calls:**
  - Through function designators and function pointers. Variadic functions, with `va_start`/`va_arg`/`va_end`/
    `va_copy` over the interpreter's own argument list.
  - Struct arguments and struct return values by value.
  - Recursion depth is limited by `RunOptions::maxCallDepth`.
- **Library functions:** a function without a definition is looked up among the evaluator's builtins and in the
  library ("Library"; the order is under "Functions without definition" below). A function found neither as a
  definition nor there returns a value from the oracle (`UndefinedFunction`), and its arguments have no effect.
- **Undefined behaviour:**
  - Operations without a defined result end the run with `Termination::UndefinedBehavior` and a message: memory
    errors, integer division by zero, a call through a pointer to no function, a non-void function falling off its
    end whose value is used.
  - Signed overflow and invalid shifts are reported through `onUndefinedBehavior` and continue with the hardware
    result.

## Evaluator: how it is built, and its behaviour choices

The implementation is split by area: `lowering.cpp` (`buildProgram`, `program.hpp`), `execution.cpp` (start-up,
ending, limits), `statements.cpp` (the instruction loop), `calls.cpp`, `expressions.cpp`, `lvalues.cpp`,
`conversions.cpp`, `initialization.cpp`, `builtins.cpp`, `jumps.cpp` and `events.cpp`; `execution.hpp` declares the
class `Execution` they share, `type_info.hpp` the representation of types (computed without canonicalizing, because
the evaluator asks for it at every operation), and `host_stack.hpp` the check of the host stack.

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
  are limited too. The interpreter executes about 7.4 million steps per second.
- **Endless loops without effect:** the lowering marks a jump back over instructions that are all statement events or
  branches on a nonzero integer literal or without condition (`Instruction::closesEndlessLoop`): `L: goto L;`,
  `while (1);`, `for (;;) {}`, `do ; while (1);`. Once the jump is taken, the function runs these instructions for
  ever, and they change nothing: no expression other than an integer literal is evaluated (so no call, no input, no
  volatile read), no declaration or end of scope runs. The pattern is narrow on purpose: a condition `x`, or
  `if (0) ;` in the body, is not marked (the jump of `continue` in `while (1) { continue; }` is). Endless loops it
  misses, such as `for (;;) { continue; }`, `while ('a');` or `if (1) ; else ;` in the body, run on.
  With `RunOptions::endsEndlessLoops`, which validation sets, the run ends with `Termination::EndlessLoop` at the
  second time the jump is taken, after one whole round, at the location of the round's first instruction. Replay does
  not set it and runs on, as the native program hangs: the difftest harness reports a timeout for both, and a run that
  hits a limit agrees with nothing, so ending replay early would change no comparison but would make replay differ
  from the native run it mirrors.
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
- **Records** keep the initialization state and the terms of their bytes when they are copied: assignment and
  initialization from another record, arguments (copied into a temporary when the argument is evaluated, then into the
  parameter), results (copied into a temporary of the caller before the callee's frame ends), `?:` and the comma
  operator (which select an object). An assignment between partly overlapping objects is undefined
  (C17 6.5.16.1p3). Known limitation: copying an uninitialized record copies its uninitialized state, so reading the
  same member of both copies asks for two independent inputs, while a native run reads equal values.
- **Uninitialized reads:** a scalar with uninitialized bytes is answered by the oracle; bytes that were written keep
  their values (then the value is opaque). Every answer is normalized to the requested type (`_Bool` 0 or 1,
  integers truncated). A bit-field whose storage has uninitialized bytes asks for those bytes (as `unsigned char`
  inputs), stores them, and reads the field from them; neighbouring fields in the same bytes read the same input.
- **Conversions:** a pointer converted to a wider integer is sign-extended (GCC, "Arrays and pointers"). Every
  conversion between scalar types records its term (see "Terms").
- **va_list:** the object holds a handle (a number in its first four bytes) of a cursor over the variadic arguments of
  the frame that called `va_start`. A copy of the object (passing a va_list) shares the cursor, as on x86-64;
  `va_copy` makes an independent one. `va_arg` with a type of another kind or size than the argument's (C17
  7.16.1.1p2; signed and unsigned versions of a type, and integers and pointers of the same width, are accepted),
  after the last argument, or after the function returned is undefined. Library functions with a va_list get the
  remaining arguments instead.
- **Calls through a pointer of another type** (C17 6.5.2.2p9) convert scalar arguments and results like the
  registers that pass them; a struct where a scalar is expected, or the other way round, is undefined. The conversion
  of a result with a term starts from the format of its term, which is the format of the type the callee returned.
- **Pass-through builtins** (`__builtin_expect`, `__builtin_expect_with_probability`, `__builtin_assume_aligned`)
  return their first argument converted to the result type, with the term of the conversion.
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

## Non-local jumps

`setjmp`, `_setjmp`, `__sigsetjmp` and `sigsetjmp` save an environment, `longjmp`, `_longjmp`, `siglongjmp` and
`__longjmp_chk` jump to it (C11 7.13; there are no signals, so the signal mask does not matter). They are builtins of
the evaluator (`jumps.cpp`); a program's own definition of one of the names wins, as for other builtins.

- **Environments:** setjmp saves its frame, the instruction it is evaluated in, the call, and the number of the frame's
  dynamic objects (variable length arrays and alloca blocks), writes the environment's id into the first 8 bytes of
  the jmp_buf (glibc's has 156 or 200 bytes) and returns 0. So a jmp_buf works wherever it lives (static, automatic,
  heap memory) and in copies (assignment, `memcpy`). A second call at the same site in the same frame, as in a loop,
  saves under the same id, so that the environments do not grow with the iterations. They end with their frame. A
  setjmp in the initializer of a static object has no frame: sema accepts such initializers although C17 6.7.9p4
  requires them to be constant, and the interpreter evaluates them before `main`, so the run ends as `Unsupported`.
- **longjmp** reads the id. If the bytes hold no live environment, the behaviour is undefined (C11 7.13.2.1p2) and the
  run ends with `UndefinedBehavior`: setjmp did not save it (zeros, other bytes, or uninitialized bytes, which are no
  input), or the function that called setjmp has returned (also a wrapper around setjmp, as busybox's `_setjmp`).
  Otherwise an exception unwinds the host stack to the instruction loop of the environment's frame. The frames between
  end as at a return (their locals, variable length arrays, alloca blocks, temporaries, va_list cursors and
  environments), but without a `ReturnEvent`: their functions do not return. In the frame itself, the dynamic objects
  allocated after the setjmp call end, since longjmp restores the stack pointer.
- **Resuming:** the frame evaluates the instruction of the setjmp call again, and the call returns longjmp's value, or 1
  for 0 (C11 7.13.2.1p4), without being called again: no `CallEvent` and no statement event, but a `ReturnEvent` with
  the new value, then the events of the rest of the instruction (the condition of `if (setjmp(env))` again). This is
  the native behaviour only if nothing that the instruction evaluates before the call has an effect, or a value that
  may have changed. So a longjmp resumes only where everything on the way from the instruction's expression to the call
  is a parenthesis, a cast, `!`, a comparison with an integer constant expression, or an assignment to a variable (a
  member of one, an element of an array variable with a constant index), and the instruction is an expression
  statement, a controlling expression or an initializer. These are the uses of C11 7.13.1.1p4, and `x = setjmp(env);`
  and `int x = setjmp(env);`, which GCC and Clang support too and sv-benchmarks uses. A longjmp to a setjmp in any
  other expression ends the run as `Unsupported`, and so does one to a setjmp called through a function pointer, in a
  statement expression, or in a function that evaluated a variably modified type before the call: a jump back into the
  scope of a variable length array is undefined once the scope was left in the interim (C11 7.13.2.1p2), which the
  interpreter does not track.
- **Automatic objects changed between setjmp and longjmp:** C11 7.13.2.1p3 makes those of the frame of setjmp that are
  not volatile indeterminate. The interpreter keeps the values they had at the longjmp, as GCC and Clang do at -O0,
  where every object lives in memory. The kept value is one of the values that an indeterminate object may have (C11
  3.19.2), so a run with it is an execution of the program. sv-benchmarks expects the same:
  `longjmp/68-longjmp_42-poison-reduced_true` asserts `x == 2` after the jump for an `x` set to 1 and 2 before it, as
  the Goblint test does that it comes from. Treating such objects as inputs instead would let a witness choose a value
  that the expected verdict excludes, and confirm a violation of that correct program.
- **Terms:** the value that setjmp returns after a longjmp has the term of `val == 0 ? 1 : val`, with the operand the
  run selected (`TermKind::Conditional`), so that the search can choose the inputs that `val` derives from. Whether a
  longjmp happens, and to which environment, depends on inputs only through control flow, which terms do not record
  ("Terms", "Known imprecisions").

## Terms

Every value the interpreter derives from an input carries a term (`value/README.md`, "Terms") in the run's
`TermTable` (`EvaluationContext::terms()`), so that the validator can predict values for other inputs. Replay records
none (`RunOptions::terms`, `maxTerms` 0). Each operation records the term of its result and attaches it
(`Execution::attach`); `terms.cpp` has the consistency check.

- **Inputs:** every input request (`__VERIFIER_nondet_*`, a function without definition, an uninitialized read)
  registers its answer, normalized to the requested type, as an input (`TermTable::input`); a `_Bool` gets a ToBool
  term over it (below).
- **Operations:** where the evaluator applies an operation of arithmetic.hpp, it calls the `TermTable` function of
  the same name: unary and binary arithmetic, shifts, comparisons, compound assignments, `++` and `--` (with the
  promotion and the conversion back), and every conversion between scalar types, also to `_Bool`. `!`, `&&`, `||` and
  `?:` record their own kinds with the operands the run evaluated (GNU `c ?: b` is `c ? c : b`); pass-through builtins
  record the conversion of their argument; atomic read-modify-writes their operation. Undefined behaviour that the
  run continues after gives the value the run continues with, which is also what the term gives; division by zero
  ends the run before a term is recorded.
- **Pointers converted to wider integers** are sign-extended (see "Conversions"): the term converts to the signed
  integer of the pointer's width first, and then to the target, so that it gives the run's value.
- **`_Bool` values** have the format of `unsigned char`, so that a term of that format may give other values than 0
  and 1; a ToBool term gives only those. A conversion to `_Bool` (`Execution::toBool`; also of the arguments and
  results of calls with `_Bool` parameters and results) records a ToBool term, unless the operand's term is one
  already, also behind integer conversions, which keep 0 and 1 (`c ? b : d` promotes `_Bool` operands to `int`): then
  the result has that term. Inputs of type `_Bool`, also uninitialized `_Bool` objects, get a ToBool term over their
  input term (`requestInput`). A `_Bool` read from bytes written as another type, and `?:` of `_Bool` values with a
  condition that depends on inputs, keep their terms up to the next conversion.
- **Copies keep the term:** assignment, initialization, arguments, results, `va_arg` (with a conversion between the
  signed and the unsigned type), and memory (below).
- **Conditions:** every `ConditionEvent` (`if`, loops, `?:`, `switch`) carries the term of the controlling
  expression's value, for a `switch` that of the promoted value, which `switchValue` carries too. A `for` without
  condition has none.
- **Constraints** of the observer are evaluated in a `TermTable::StrictScope`. The terms of an input that a constraint
  requests (the first read of an uninitialized object) belong to the program, which stores the input: they are not
  strict.
- **Opaque values:** a value computed from an operand with a term by an operation without a term kind gets
  `TermTable::opaque()`, so that it never becomes a constant of a later term:
  - library functions: the result, if an argument has a term or the function reads bytes that depend on inputs
    (`Memory::beginRawAccesses`); so also a pointer that `strchr`, `strstr`, `memchr` and the like compute from an
    input-derived pointer. The bytes the function writes with `write` and `fill` become opaque if it reads such bytes
    or if an argument other than a pointer has a term (also a size: `memset(p, 0, n)` with an input-derived `n` makes
    the zeros opaque). A pointer argument only tells where the bytes are, as the address of a store does, unless the
    function writes a value computed from a pointer argument (`LibraryResult::writesPointerValues`): a pointer printed
    with `%p` by `sprintf` and its relatives, the end pointer of `strtol` and its relatives. Functions that copy bytes
    (`memcpy`, `memmove`, `strcpy`, `strdup`, `realloc`, ...) copy their terms (`Memory::copy`); `memset` with
    arguments without terms clears them;
  - pointer arithmetic (`p + i`, `&a[i]`, `p - q`, `++p`, `&p->member` past offset 0) with a pointer, an index or an
    element size (a variable length array) that depends on inputs, and `sizeof` of such an array;
  - bit-fields: a field read from storage that depends on inputs, and the storage after it got a value with a term or
    input bytes, or after another field of such storage was written;
  - the overflow builtins, `alloca` with an argument with a term, and the comparisons of atomic compare-and-swap;
  - a struct or union value whose bytes depend on inputs, when it is written as bytes (`loadRecord`, `writeRecord`; a
    value from `va_arg` or a statement expression, a GNU cast to a union); copies of objects keep the exact terms;
  - a scalar read from memory whose bytes are not exactly those of one value written with a term (below), or read
    partly from uninitialized bytes (the input fills only those bytes).
- **Memory** keeps terms per address range (`Memory::term`). A scalar written with a term records it with the format
  it was written in. A read of exactly that range gets the term back: through the same format as it is, through
  another integer format of the same size (signed and unsigned, a pointer) with the conversion, through a floating
  type where an integer was written (or the other way round: type punning) as opaque. A read of a part of the range,
  or of a range that contains it, is opaque. A write that overwrites only a part of a range leaves the rest opaque.
  `copy` carries the terms; a range that lies only partly in the copied bytes is opaque in the copy. Bytes written
  without term (`fill`, string literals, initializers) have none; the end of an object's lifetime and
  `markUninitialized` remove them.
- **The consistency check** (`RunOptions::checkTerms` with `termMismatch`, the hidden replay option `--check-terms`)
  evaluates every term that an operation records (`Execution::attach`) or a load reads from memory, also the strict
  terms of constraints, with the run's inputs and compares the result with the value bit for bit; a mismatch ends the
  run with "internal error: the term of <operation> gives ...". A mismatch found while the observer evaluates a
  constraint fails that evaluation, and the run ends with the first one when the observer returns.
  - Terms set by construction are not evaluated: the input terms of answers (`requestInput`), a pointer's term taken
    from the address of its object (`pointerTo`), the result of a call that keeps its callee's term because the term
    has the format of the call's type (`inCallType`), and the opaque term. They are exact because they pass a term
    on unchanged.
  - The check cannot see a term that is missing where a value depends on inputs. `terms_test.cpp` covers those: it
    expects the term of each kind of operation, copy and conversion, and the opaque term from each operation without
    a term kind (above).
  - The semantics suite runs with it, and so do the end-to-end tests of the validator, which evaluate constraints.
    `terms_test.cpp` also passes a check that finds the strict terms wrong on purpose, for how a mismatch in a
    constraint ends the run.
- **Known imprecisions:** these dependencies are not recorded.
  - Implicit flows: a value assigned under a condition that depends on an input (`if (x > 0) y = 1;`) does not depend
    on that input. Which branch the run takes is the waypoints' business, not the terms'.
  - Addresses: a value read from or written to an object does not depend on the object's address. `a[i]` with an
    input-derived `i` has the term that `a[i]` holds in this run (none for constant elements), although another `i`
    reads another element; the pointer `&a[i]` is opaque. Table lookups are such reads: glibc's ctype macros read
    `isdigit(c)` as `(*__ctype_b_loc())[c] & 2048`, which has no term for an input-derived `c`. Equally, which object
    a struct `?:` selects is not recorded. Addresses also depend on the sizes of earlier objects: after
    `p = malloc(n)` with an input-derived `n`, `p` is opaque, but `q = malloc(4)` has no term, although where it lies
    depends on `n`; so do the addresses of objects after a variable length array of input-derived size.
  - Over-approximations, which make values opaque that the inputs do not change: opaque bit-field storage stays
    opaque until it is overwritten as a whole without term; all bytes a library function writes are opaque if one
    depends on inputs.
  - The limits of the table (`value/README.md`, "Terms"): results beyond the depth or the size limit are opaque.
- **Cost:** operations ask the `TermTable` only if an operand has a term, and memory looks up terms only if it holds
  any, so that values without terms (programs without inputs) cost nothing measurable. Two details matter on this
  path: the operations return one named value each, so that it is constructed in place, and `LValue` fits into two
  registers. The cost of the terms, measured with `gcc-release` (CPU time, best of 9 runs on one core) against an
  interpreter that records only which input a value is a copy of: Csmith programs without inputs from 6 % faster to
  3 % slower (+0.4 % in all); input-heavy corpus programs from 12 % faster (tracking the copied inputs cost them more
  than the terms do) to 7 % slower; Csmith programs whose globals all depend on inputs 13 % to 19 % slower, with up to
  124 MiB for a full table.

## Tests

- `tests/unit/interp/memory_test.cpp`: the memory behaviour above:
  - allocation in every region with sizes 0, 1 and large; alignment; the null page; gaps between allocations;
  - accesses inside, across the end and one past the end; dead heap blocks, merged into ranges (the size of the table
    in a `malloc`/`free` loop, live blocks between dead ones, the gaps of a range); double free; freeing non-base and
    non-heap addresses; LIFO stack reuse;
  - read-only and code protection; initialization per byte;
  - terms written, read back for exactly their range (with their format) and for parts of it (opaque), partly
    overwritten (the rest opaque), marked opaque, copied (overlapping ranges too, parts of ranges opaque), dropped at
    the end of a lifetime; the raw accesses of library functions;
  - deterministic addresses; the limits of the 32-bit address space; a 1 GiB global written at two places only.

  `Memory` was also checked against a per-byte reference model, without a mismatch; that model is not part of the
  repository.
- `tests/unit/interp/`: interpreter tests on small programs (source text → output and termination), with a test
  oracle and a recording observer for the event sequences (`interpreter_test_support.hpp`): `lowering_test.cpp`
  (statements, conditions, jumps and calls as events), `evaluator_test.cpp` (operators, conversions, objects and
  calls in both data models, outputs confirmed with GCC), `termination_test.cpp` (how runs end, undefined behaviour,
  limits, endless loops without effect and loops that only look like them), `inputs_test.cpp` (oracle requests,
  uninitialized reads), `jumps_test.cpp` (setjmp and longjmp in both data models: the values setjmp returns, every
  name, the frames a longjmp ends (to `main`, to a frame between, out of a statement expression) and its events, kept
  values of changed locals, copied and heap jmp_bufs, setjmp in a loop, each use that resumes, the dynamic objects that
  end, and the jumps that end the run: to a function that returned, without setjmp, from other expressions, through a
  pointer, in a statement expression, after a variably modified type, GCC's builtins, setjmp in the initializer of a
  static object) and
  `evaluation_context_test.cpp`.
- `tests/unit/interp/terms_test.cpp`, every run with the consistency check: input terms; copies (assignment,
  arguments, results, pointers, records, `?:` of records, `va_arg`); conversions (also pointers sign-extended to wider
  integers and back, results of calls through a pointer of another type); `_Bool` values (inputs, bytes of another
  type, arguments and results, a chain of 40 calls, conversions through a narrowing); integer and floating
  arithmetic, shifts, `++`, `--`, compound assignments (also with signed overflow) and atomic read-modify-writes;
  comparisons, `!`, `&&`, `||` (with the operand the run did not evaluate) and `?:` (also GNU `?:`); the value of a
  longjmp; opaque values
  from partial overwrites, partly initialized reads, type punning, library functions (and that copying functions keep
  terms, and those that write pointer values do not), pointer arithmetic, bit-fields, variable length array sizes,
  the overflow builtins, `alloca` and `__VERIFIER_nondet_memory`; table lookups without term; the terms of condition
  events; the depth limit, conversions of inputs in a full table (also a loop of them, followed by an input) and runs
  without terms; strict terms of constraints, and not strict the program's terms after them and those of an input
  that a constraint reads first; the check's comparison (`termMismatch`) with wrong terms, formats and floating
  values, and a check that finds strict terms wrong: the run ends with the first mismatch when the observer returns.
- `tests/semantics/`: the programs of the semantics suite (see `tools/difftest/README.md`) must print the expected
  output under `-32` and `-64`, also with the consistency check of the terms (CTest label `semantics`).
- `tools/difftest/`: `difftest.py run --nitwit` compares the interpreter with the GCC and Clang builds of one program
  with the same inputs; run per program on Csmith programs and on corpus programs with random inputs, it must agree
  with them (results in `README.md`, "Results").
