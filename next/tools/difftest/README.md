# Differential testing against GCC and Clang

A program is run natively, compiled by GCC and by Clang, and by the interpreter, all with the same nondeterministic
inputs; the observable behaviour must agree. Until the interpreter exists (stage 4), the tools compare the two
compilers. That comparison is useful on its own: programs on which GCC and Clang disagree, or on which UBSan or ASan
fire, depend on undefined or implementation-defined behaviour, and are excluded from interpreter comparisons.

## Contract with the interpreter

The driver will offer (stage 4):

```
nitwit --replay <inputs-file> --input-log <log-file> [-32|-64] <program.c|.i>
```

- **Input and output:** it runs `main` with the inputs from `<inputs-file>`, writes the program's output to stdout,
  and writes the input log like the harness does. Only `__VERIFIER_nondet_*` calls take lines and write log entries.
- **Exit status:** it exits with the program's exit status (the value returned by `main` or passed to `exit`, modulo
  256), or with the codes and markers of the harness below. For `abort` it ends by `SIGABRT`; like the harness, it
  first makes itself non-dumpable (`prctl(PR_SET_DUMPABLE, 0)`), since WSL's `core_pattern` helper collects core
  dumps despite `RLIMIT_CORE`.

## Files

| File | Purpose |
|---|---|
| `harness.c` | linked to the program natively; implements the SV-COMP functions (below) |
| `difftest.py` | library and command line: compile, run, compare; `python3 difftest.py --help` |
| `inputs.py` | generates input files from a seed |
| `csmith.py` | generates Csmith programs (needs `csmith` and `libcsmith-dev`) and preprocesses them to `.i` with GCC |
| `corpus.py` | runs a sample of the dataset programs with random inputs under both compilers, writes a TSV |

Requirements: Python 3.10 or later (standard library only), `gcc-14` with `gcc-14-multilib`, `clang-20`, and
`systemd-run --user` for containment (see "Native runs").

```bash
python3 tools/difftest/difftest.py run prog.c --bits 32 --inputs in.txt -v  # GCC, Clang and Clang+sanitizers
python3 tools/difftest/difftest.py semantics [--update] [name...]           # tests/semantics
python3 tools/difftest/corpus.py run --size 500 -o ~/difftest-out/corpus.tsv [--resume]
python3 tools/difftest/csmith.py --seed 1 --count 20 --bits 32 --out ~/difftest-out/csmith --check
```

## Harness

- **Inputs:**
  - `__VERIFIER_nondet_<type>()` for the types that sv-benchmarks declares without defining them: `int`, `uint`,
    `long`, `ulong`, `longlong`, `ulonglong`, `char`, `uchar`, `short`, `ushort`, `bool`, `float`, `double`, `pointer`,
    `size_t`, `u8`, `u16`, `u32`, `unsigned`, `loff_t` (`long long`), `pchar`, `sector_t` (`unsigned long`), `charp`,
    `pthread_t` (`unsigned long`), and `uint128` on 64-bit targets. Each takes the next line of the file named by
    `NITWIT_INPUTS`.
  - `__VERIFIER_nondet_memory(void *, size_t)` fills the memory byte by byte; each byte takes a line, as `uchar` does.
  - Integer functions read a decimal integer: `[+-]digits`, leading zeros allowed, no spaces. It is read modulo 2^64
    (2^128 where `__int128` exists) and converted to the return type as a C cast would (GCC and Clang wrap).
  - `float` and `double` read a decimal integer, a C99 hexadecimal float with exponent as `%a` prints it
    (`[+-]0x<hex>[.<hex>]p[+-]<decimal>`), `inf`, `-inf`, `nan` or `-nan`, convert it to `double` with `strtod` (exact,
    or correctly rounded for long integers), and then to the return type.
  - A line has at most 255 characters without its line break; a `\r` before the `\n` is removed.
  - When the file is exhausted, or `NITWIT_INPUTS` is not set or empty, the value is 0.
- **Log:** every value handed out is appended to the file named by `NITWIT_INPUT_LOG`, as
  `<function> <value>[ default]`, where `default` marks an exhausted input file. The value is the converted one:
  decimal for integers (signed or unsigned as the return type, `char` is signed), decimal addresses for pointers, `%a`
  for `float` and `double`.
- **Other functions:** `__VERIFIER_atomic_begin` and `__VERIFIER_atomic_end` do nothing (programs with threads are not
  compared, see "Comparison"). The harness also defines `reach_error`, for programs that only declare it.
- **Weak symbols:** every harness function is weak, so that a definition in the program wins.
- **Process:** stdout is unbuffered, so that runs ending by a signal keep their output. The process is not dumpable, so
  that an abort or a crash writes no core dump.
- **Exit codes:** the harness ends the run at once, after flushing stdout, without running `atexit` handlers.

  | Event | Marker on stderr | Exit code |
  |---|---|---|
  | `__assert_fail` (what sv-benchmarks' `reach_error` calls), `__VERIFIER_error`, `reach_error` | `NITWIT-DIFFTEST: error <function>` | 97 |
  | `__VERIFIER_assume` with a false argument, i.e. the run is infeasible | `NITWIT-DIFFTEST: assume` | 98 |
  | an input line that the function cannot read, or an inputs file that cannot be opened | `NITWIT-DIFFTEST: bad input: ...` | 99 |
  | `abort` | – | signal (exit status 134) |

## Native runs

- **Compilation:** `gcc-14` and `clang-20`; `-m32` and `-m64`; `-O0 -w -fno-builtin -fno-stack-protector`; and for
  x87-free floating point with `-m32`, `-msse2 -mfpmath=sse`. GCC 14 and Clang reject some pre-C99 code of
  sv-benchmarks (implicit `int`, implicit function declarations, int/pointer conversions); `-fpermissive` (GCC) and
  `-Wno-error=...` (Clang) accept it again.
- **Builds:**
  - GCC, plain;
  - Clang with `-ftrivial-auto-var-init=pattern`, run with `MALLOC_PERTURB_=165`: uninitialized automatic variables
    and heap blocks hold a pattern, so a program that reads uninitialized memory disagrees with the GCC build;
  - a build with `-fsanitize=undefined,address -fno-sanitize-recover=all` (by default with Clang), which detects runs
    with undefined behaviour; leak detection is off.
- **Containment:** every compile and every run happens in a transient systemd scope (`systemd-run --user --scope`)
  with `MemoryMax` (the memory limit), no swap and at most 64 tasks; the scope is stopped afterwards, which also kills
  processes that left the run's session. Without `systemd-run`, the tools warn and rely on the limits below.
- **Limits:** each run has a wall-clock timeout; `ulimit` limits on CPU time (soft: `SIGXCPU`; hard, one second later:
  `SIGKILL`), file size and address space; and its own session. Inherited hard limits that are lower are respected,
  with a warning. ASan reserves terabytes of address space, so sanitizer builds get ASan's `hard_rss_limit_mb` and
  `max_allocation_size_mb` instead of the address-space limit; under an inherited address-space limit they cannot run,
  and the tools stop with a message. Defaults: 2 GiB per run and 4 parallel jobs.
- **Environment:** the program runs in an empty temporary directory, with `argv[0]` = `prog` and only `PATH`,
  `LC_ALL=C`, `HOME=/nonexistent`, `TMPDIR=/tmp`, `NITWIT_INPUTS` and `NITWIT_INPUT_LOG` set (plus the sanitizer
  options or `MALLOC_PERTURB_` of its build). The inputs, the input log and the captured stdout and stderr are in a
  sibling directory.
- **Untrusted programs:** dataset programs are research benchmarks, but they are still untrusted. Run them only through
  this tool, never by hand in the dataset directory.

## Comparison

A run's outcome is one of:
- `exit:<code>`, `error`, `assume`, `bad-input` (exit codes 97 to 99 with their marker), `abort`, `signal:<name>`;
- `sanitizer` (an ASan or UBSan report);
- a limit: `timeout` (wall clock or `SIGXCPU`), `killed` (`SIGKILL`: hard CPU limit or memory cgroup), `output-limit`
  (`SIGXFSZ`), `memory-limit` (ASan's RSS limit), `stack-overflow` (ASan).

Two runs agree if:
- their outcomes are the same (the exit status, or the signal, with the harness codes);
- their marker lines on stderr are the same;
- their stdout is the same;
- their input log is the same.

A run that hits a limit agrees with nothing. Programs that create threads (`pthread_create` or `thrd_create`) are not
run: they are nondeterministic natively and not supported by the interpreter. Resource exhaustion that a native run
cannot tell from program behaviour (`malloc` returning null under the memory limit, a stack overflow without ASan)
is only caught where another build reports it.

## Semantics tests

`tests/semantics/*.c` are small programs that print results with `printf`. Each covers one area of C:
- integer promotions and conversions;
- the usual arithmetic conversions;
- shifts and bit-fields;
- struct layout and `offsetof`;
- unions;
- pointer arithmetic and comparisons;
- arrays and VLAs;
- `switch`, `goto` and loops;
- recursion;
- function pointers;
- variadic functions;
- string and memory functions;
- `malloc`/`free`;
- floating point.

The `contract_*` programs test the input contract above: errors, assumptions, `abort`, exit statuses, and the less
common nondet functions.

The programs declare the library functions they use instead of including headers, since the interpreter's lexer does
not run the preprocessor. Only `int128.c` and `contract_nondet.c` use `#ifdef __SIZEOF_INT128__`, so the driver runs
`cpp` for them. `<name>.inputs` holds the values of their `__VERIFIER_nondet_*` calls.

Next to each program are the expected results, which `difftest.py` checks for GCC and Clang, also in the sanitizer
builds of both compilers:
- `<name>.expected-32` and `<name>.expected-64`: the output;
- `<name>.expected-status`: the outcome, if it is not `exit:0`; stderr may only hold the harness's markers;
- `<name>.expected-log-32` and `<name>.expected-log-64`, optional: the input log.

`difftest.py semantics --update` writes the output and status files, and the log files that exist, when all builds
agree. Once the interpreter exists, CTest (label `semantics`) checks the interpreter against the same files.
