# Module `driver`

The `nitwit` executable: command line, reading files, running the steps below, printing the result, exit code.

## Command line

```
nitwit [-32|-64] -w|--witness <witness> [-p|--property <property-file> | -e|--error-function <name>] [-v]
       [--strict-inputs] [--write-witness <file> [--no-witness-check]] <program>
nitwit --replay <inputs-file> [--input-log <log-file>] [-32|-64] <program>
nitwit --version
```

- **Data model:** `-32`/`-64` select ILP32 or LP64. Without either, the executable's name decides: LP64 if its file
  name is exactly `nitwit64` (`bin/nitwit64` of the release archive, `docs/release.md`), else ILP32 (the default, as
  in 3.x).
- **Error function:** read from the property file. `CHECK( init(main()), LTL(G ! call(reach_error())) )` and the
  `__VERIFIER_error` variant are supported; any other property is reported as UNKNOWN (exit 4). Without `-p` and
  `-e`, it is `reach_error`. Giving both is a wrong command line (exit 3).
- **Without `-w`:** a program without witness and without `--replay` gives exit code 240 ("no witness given (-w)" on
  stderr).
- **Wrong command lines** give exit code 3 and print the usage on stderr: unknown options; an option without its
  value or with an empty value; an option with a value given twice; `-32` or `-64` twice, or both; `--terms` and
  `--check-terms` together or twice; `--strict-inputs` or `--no-witness-check` twice; a second program, no program;
  `--replay` with validation options (`-w`, `-p`, `-e`, `-v`, `--strict-inputs`, `--write-witness`,
  `--no-witness-check`); `--input-log`, `--terms` or `--check-terms` without `--replay`; `--no-witness-check` without
  `--write-witness`. `-v` and `--version` may be repeated. With `--version`, nitwit
  prints the version and exits with 0 if every argument is well-formed by itself. `command_line.hpp` parses the
  command line.
- **`-v`:** prints the validation log (segments reached, restarts, why each run ended) to stderr. (NITWIT 3.x's
  `nitwit.sh -v` printed the version, which is `--version` here.)
- **`--strict-inputs`:** accepts only the inputs that follow `function_return` waypoints give, without defaults,
  search or restarts (`src/validate/README.md`, "Strict inputs"). It checks that a witness is concrete.
- **`--write-witness <file>`:** when the witness is confirmed, writes the confirming run as a concrete witness in
  format 2.2 to the file, after a validation of it with strict inputs confirmed it (`src/validate/README.md`,
  "Concrete witnesses"). One line on stderr says what happened: `nitwit: wrote a concrete witness with 3 inputs to
  'w.yml'`, or `nitwit: no witness written:` and the reason. It comes after the result line, and neither the verdict
  nor the exit code depends on it. Writing repeats the confirming run once, within the time limit of the
  validation; the fm-tools entry does not pass the option.
- **`--no-witness-check`:** with `--write-witness`, writes the concrete witness without that check, and the line says
  `wrote an unchecked concrete witness`.
- **`--replay`:** the differential-testing mode (`tools/difftest/README.md`): runs `main` with the inputs file, without
  a witness. The program's output goes to stdout, and the exit status is the program's (or the harness codes).
  - Calls of `__assert_fail`, `__VERIFIER_error` and `reach_error` without a definition end the run with the harness's
    marker and code 97, a failed `__VERIFIER_assume` with 98, an input line that cannot be read with 99; `abort` ends
    the process by `SIGABRT`, `__builtin_trap` by `SIGILL`.
  - A run the interpreter cannot finish (undefined behaviour without a defined result, an unsupported construct, a
    limit) ends with code 4 and a message on stderr, one that exceeds the memory limit with 251. A program the front end
    rejects also gives 4, with its diagnostics; an unreadable file gives 3.
  - The process is not dumpable, so that `abort` and crashes write no core dump.
  - Replay records no terms. The hidden options `--terms` and `--check-terms`, for testing the interpreter, record
    them as validation does, and the second also checks each term against the run's value
    (`interp::RunOptions::checkTerms`; a mismatch ends the run with code 4 and "internal error" on stderr). They are
    not in the usage text.

## Steps

Everything after reading the command line runs on one thread with a 1 GiB stack (`runWithLargeStack`). The parser,
sema and the interpreter all recurse on it.

| Step | On failure | Exit code |
|---|---|---|
| 1. Read the witness and the property file, and take the error function from the property | a file cannot be read | 3 |
| | a property other than `unreach-call` | 4 |
| 2. Read and lex the program; if it needs the preprocessor, run `cpp -m32/-m64` on it and lex the output (line markers kept) | the program cannot be read | 3 |
| | `cpp` fails, or lexing fails | 4 |
| 3. Parse, analyze, build the program | | 4 |
| 4. Detect the witness format; parse a witness in format 2.x | a witness in format 1.0 (GraphML, not supported) | 4 |
| | the witness cannot be parsed | 2 |
| 5. Validate the witness (`src/validate/README.md`) | | the verdict's code |
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
  The reason follows. A wrong command line (3) and a missing witness (240) print no result line.
- **Exit code:** BenchExec reads only the exit code (`docs/exit-codes.md`).
