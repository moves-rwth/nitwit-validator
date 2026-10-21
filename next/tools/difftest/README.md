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
  and writes the input log like the harness does.
- **Exit status:** it exits with the program's exit status, or with the codes of the harness below.

## Files

| File | Purpose |
|---|---|
| `harness.c` | linked to the program natively; implements the SV-COMP functions (below) |
| `difftest.py` | library and command line: compile, run, compare; `python3 difftest.py --help` |
| `inputs.py` | generates input files from a seed |
| `csmith.py` | generates Csmith programs (needs `csmith` and `libcsmith-dev`) and preprocesses them to `.i` with GCC |
| `corpus.py` | runs a sample of the dataset programs with random inputs under both compilers, writes a TSV |

Requirements: Python 3.10 or later (standard library only), `gcc-14` with `gcc-14-multilib`, `clang-20`.

```bash
python3 tools/difftest/difftest.py run prog.c --bits 32 --inputs in.txt -v  # GCC, Clang and Clang+sanitizers
python3 tools/difftest/difftest.py semantics [--update] [name...]           # tests/semantics
python3 tools/difftest/corpus.py run --size 500 -o ~/difftest-out/corpus.tsv
python3 tools/difftest/csmith.py --seed 1 --count 20 --bits 32 --out ~/difftest-out/csmith --check
```

## Harness

- **Inputs:**
  - `__VERIFIER_nondet_<type>()` for every type used in sv-benchmarks (`int`, `uint`, `long`, `ulong`, `longlong`,
    `ulonglong`, `char`, `uchar`, `short`, `ushort`, `bool`, `float`, `double`, `pointer`, `size_t`, `u8`, `u16`, `u32`,
    `unsigned`, `loff_t`, `pchar`, `sector_t`, `charp`, and `uint128` on 64-bit targets). Each takes the next line of
    the file named by `NITWIT_INPUTS`.
  - `__VERIFIER_nondet_memory(void *, size_t)` fills the memory byte by byte; each byte takes a line, as `uchar` does.
  - A line is a decimal integer, or a C99 hexadecimal float (`%a`) for `float` and `double`. An integer line is read
    modulo 2^64 (2^128 where `__int128` exists) and converted to the function's return type as a C cast would (GCC and
    Clang wrap). `float` and `double` read the line with `strtod` (so decimal integers work too) and convert the
    `double` to the return type.
  - When the file is exhausted, the value is 0.
- **Log:** every value handed out is appended to the file named by `NITWIT_INPUT_LOG`, as
  `<function> <value>[ default]`, where `default` marks an exhausted input file. The value is the converted one:
  decimal for integers (signed or unsigned as the return type, `char` is signed), decimal addresses for pointers, `%a`
  for `float` and `double`.
- **Weak symbols:** every harness function is weak, so that a definition in the program wins. The harness also defines
  `reach_error`, for programs that only declare it.
- **Output:** stdout is unbuffered, so that runs ending by a signal keep their output.
- **Exit codes:** the harness ends the run at once, after flushing stdout, without running `atexit` handlers.

  | Event | Marker on stderr | Exit code |
  |---|---|---|
  | `__assert_fail` (what sv-benchmarks' `reach_error` calls), `__VERIFIER_error`, `reach_error` | `NITWIT-DIFFTEST: error <function>` | 97 |
  | `__VERIFIER_assume` with a false argument, i.e. the run is infeasible | `NITWIT-DIFFTEST: assume` | 98 |
  | an input line that the function cannot read, or an inputs file that cannot be opened | `NITWIT-DIFFTEST: bad input: ...` | 99 |
  | `abort` | – | signal (exit status 134) |

## Native runs

- **Compilation:** `gcc-14` and `clang-20`; `-m32` and `-m64`; `-O0 -w -fno-builtin`; and for x87-free floating point
  with `-m32`, `-msse2 -mfpmath=sse`. GCC 14 and Clang reject some pre-C99 code of sv-benchmarks (implicit `int`,
  implicit function declarations, int/pointer conversions); `-fpermissive` (GCC) and `-Wno-error=...` (Clang) accept it
  again. A second build with `-fsanitize=undefined,address -fno-sanitize-recover=all` (by default with Clang)
  detects runs with undefined behaviour; leak detection is off.
- **Limits:** each run has a wall-clock timeout, `ulimit` limits on address space, file size and CPU time, and a fresh
  temporary working directory. ASan reserves terabytes of address space for its shadow memory, so sanitizer builds get
  ASan's `hard_rss_limit_mb` and `max_allocation_size_mb` instead of the address-space limit.
- **Untrusted programs:** dataset programs are research benchmarks, but they are still untrusted. Run them only through
  this tool, never by hand in the dataset directory.

## Comparison

Two runs agree if:
- their exit status is the same (including the harness codes);
- their stdout is the same;
- their input log is the same.

A run that hits a limit (time, output size, or the memory limit of a sanitizer build) agrees with nothing.

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

The programs declare the library functions they use instead of including headers, since the interpreter's lexer does
not run the preprocessor. `<name>.inputs` holds the values of their `__VERIFIER_nondet_*` calls.

Next to each program, `<name>.expected-32` and `<name>.expected-64` hold the output, and `difftest.py` checks that GCC
and Clang print exactly that, also in the sanitizer builds of both compilers, which must exit with 0 and print nothing
on stderr. `difftest.py semantics --update` writes the expected files when all builds agree. Once the interpreter
exists, CTest (label `semantics`) checks the interpreter against the same files.
