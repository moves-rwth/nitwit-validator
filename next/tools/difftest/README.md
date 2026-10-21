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
| `csmith.py` | generates Csmith programs (needs `csmith`) and preprocesses them to `.i` |
| `corpus.py` | runs a sample of the dataset programs with random inputs under both compilers, writes a TSV |

## Harness

- **Inputs:**
  - `__VERIFIER_nondet_<type>()` for every type used in sv-benchmarks (`int`, `uint`, `long`, `ulong`, `longlong`,
    `ulonglong`, `char`, `uchar`, `short`, `ushort`, `bool`, `float`, `double`, `pointer`, `size_t`, `u8`, `u16`, `u32`,
    `unsigned`, `loff_t`, `pchar`, `sector_t`, `charp`, `ushort`, ...). Each takes the next line of the file named by
    `NITWIT_INPUTS`.
  - A line is a decimal integer, or a C99 hexadecimal float (`%a`) for `float` and `double`. The value is converted to
    the function's return type as a C cast would.
  - When the file is exhausted, the value is 0.
- **Log:** every value handed out is appended to the file named by `NITWIT_INPUT_LOG`, as
  `<function> <value>[ default]`, where `default` marks an exhausted input file.
- **Exit codes:**

  | Event | Marker on stderr | Exit code |
  |---|---|---|
  | `__assert_fail` (what sv-benchmarks' `reach_error` calls), `__VERIFIER_error` | `NITWIT-DIFFTEST: error <function>` | 97 |
  | `__VERIFIER_assume` with a false argument, i.e. the run is infeasible | `NITWIT-DIFFTEST: assume` | 98 |
  | `abort` | – | signal (exit status 134) |

## Native runs

- **Compilation:** `gcc-14` and `clang-20`; `-m32` and `-m64`; `-O0 -w -fno-builtin`; and for x87-free floating point
  with `-m32`, `-msse2 -mfpmath=sse`. A second build with `-fsanitize=undefined,address -fno-sanitize-recover=all`
  detects runs with undefined behaviour.
- **Limits:** each run has a wall-clock timeout, `ulimit` limits on address space, file size and CPU time, and a fresh
  temporary working directory.
- **Untrusted programs:** dataset programs are research benchmarks, but they are still untrusted. Run them only through
  this tool, never by hand in the dataset directory.

## Comparison

Two runs agree if:
- their exit status is the same (including the harness codes);
- their stdout is the same;
- their input log is the same.

A run that times out agrees with nothing.

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

Next to each program, `<name>.expected-32` and `<name>.expected-64` hold the output, and `difftest.py` checks that GCC
and Clang print exactly that. Once the interpreter exists, CTest (label `semantics`) checks the interpreter against the
same files.
