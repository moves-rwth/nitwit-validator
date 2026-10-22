# Module `driver` (stage 6)

The `nitwit` executable: command line, reading files, running the stages, printing the result, exit code.

## Command line

```
nitwit [-32|-64] -w|--witness <witness> [-p|--property <property-file> | -e|--error-function <name>] [-v] <program>
nitwit --replay <inputs-file> [--input-log <log-file>] [-32|-64] <program>
nitwit --version
```

- **Data model:** `-32`/`-64` select ILP32 or LP64. Without either, the executable's name decides: LP64 if its file
  name is exactly `nitwit64` (`bin/nitwit64` of the release archive, `docs/release.md`), else ILP32 (the default, as
  in 3.x).
- **Error function:** read from the property file. `CHECK( init(main()), LTL(G ! call(reach_error())) )` and the
  `__VERIFIER_error` variant are supported; any other property is reported as UNKNOWN (exit 4). Without `-p` and
  `-e`, it is `reach_error`. Giving both is a wrong command line (exit 3).
- **Without `-w`:** a program without witness and without `--replay` gives exit code 240.
- **Wrong command lines** give exit code 3: unknown options, an option without its value, an option given twice
  (also `-32` with `-64`), a second program, no program, `--replay` with validation options, `--input-log` without
  `--replay`. `command_line.hpp` parses it.
- **`-v`:** prints the validation log (segments reached, restarts, why each run ended) to stderr. (In 3.x's
  `nitwit.sh`, `-v` printed the version; that is `--version` now.)
- **`--replay`:** the differential-testing mode (`tools/difftest/README.md`): runs `main` with the inputs file, without
  a witness. The program's output goes to stdout, and the exit status is the program's (or the harness codes).
  - Calls of `__assert_fail`, `__VERIFIER_error` and `reach_error` without a definition end the run with the harness's
    marker and code 97, a failed `__VERIFIER_assume` with 98, an input line that cannot be read with 99; `abort` ends
    the process by `SIGABRT`, `__builtin_trap` by `SIGILL`.
  - A run the interpreter cannot finish (undefined behaviour without a defined result, an unsupported construct, a
    limit) ends with code 4 and a message on stderr, one that exceeds the memory limit with 251. A program the front end
    rejects also gives 4, with its diagnostics; an unreadable file gives 3.
  - The process is not dumpable, so that `abort` and crashes write no core dump.

## Steps

Everything after reading the command line runs on one thread with a 1 GiB stack (`runWithLargeStack`). The parser,
sema and the interpreter all recurse on it.

| Step | On failure | Exit code |
|---|---|---|
| 1. Read the program and the witness | | 3 |
| 2. Lex the program; if it needs the preprocessor, run `cpp -m32/-m64` on it and lex the output (line markers kept) | `cpp` fails, or lexing fails | 4 |
| 3. Parse, analyze, build the program | | 4 |
| 4. Detect the witness format and parse the witness | | 2 |
| 5. Validate: format 2.x (stage 6), format 1.0 (stage 8, UNKNOWN until then) | | the verdict's code |
| NITWIT itself runs out of memory (`std::bad_alloc`) | | 251 |

## Output

- **Diagnostics:** each one is printed as `file:line:column: severity: message`. Errors in constraints point into the
  constraint's text (`<constraint>:1:5: error: ...`).
- **Result:** one final line starts with the verdict, as in 3.x:
  - `VALIDATED:` for a confirmed witness;
  - `FAILED:` when the witness was not confirmed;
  - `UNKNOWN:` for unsupported input and limits;
  - `ERROR:` for a witness that cannot be parsed (exit code 2) and files that cannot be read (3), which BenchExec
    reports as ERROR.
  The reason follows.
- **Exit code:** BenchExec reads only the exit code (`docs/exit-codes.md`).
