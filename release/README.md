# NITWIT

NITWIT validates violation witnesses for C programs: it executes the program with an interpreter and follows the
witness. It checks the property `unreach-call` and reads witnesses in format 2.x (YAML). Witnesses in format 1.0
(GraphML) give UNKNOWN in this version.

Source code and documentation: <https://github.com/moves-rwth/nitwit-validator>

## Usage

```
./nitwit.sh [-32|-64] -w <witness> [-p <property-file> | -e <error-function>] [-v] <program>
./nitwit.sh --version
```

- `-32` and `-64` select the data model ILP32 (the default) or LP64. `nitwit.sh` passes all arguments to
  `bin/nitwit32`. `bin/nitwit64` is the same program; called directly without `-32` or `-64`, it uses LP64.
- `-p` reads the error function from an `unreach-call` property file, e.g.
  `CHECK( init(main()), LTL(G ! call(reach_error())) )`; `-e` names it directly. The default is `reach_error`.
- `-v` prints a log of the validation to stderr. (NITWIT 3.x's `nitwit.sh -v` printed the version, which is
  `--version` here.)
- The last line of the output is the result: `VALIDATED:`, `FAILED:`, `UNKNOWN:` or `ERROR:`, then the reason. A
  wrong command line and a missing `-w` print the usage or an error on stderr instead (exit codes 3 and 240).

`./smoketest.sh` validates a small witness in both data models.

## Requirements

Linux on x86-64 with glibc 2.38 or later (Ubuntu 24.04 has 2.39; `licenses/VERSIONS` names the oldest glibc for this
build). The executables contain the C++ runtime and need no other library. Programs with preprocessor directives
other than line markers, `#line`, `#ident`, `#pragma` and the null directive are preprocessed by the `cpp` on the
`PATH`; without it, they give UNKNOWN.
Ubuntu's package `gcc-multilib` provides `cpp` and the system headers for both data models.

## Exit codes

| Code | Meaning | BenchExec |
|---|---|---|
| 0 | the witness is confirmed | FALSE |
| 2 | the witness cannot be parsed | ERROR |
| 3 | wrong command line, or a file cannot be read | ERROR |
| 4 | unsupported program, witness or property; the program aborts, its run is infeasible (a failed `__VERIFIER_assume`, an endless loop without effect) or has undefined behaviour without a defined result; a limit of NITWIT | UNKNOWN |
| 240 | no witness given | ERROR |
| 241 | the program left the witness, or called the error function before the witness reached its target | UNKNOWN |
| 242 | the program finished without calling the error function | UNKNOWN |
| 250 | the witness reached its target, but the error function was not called | UNKNOWN |
| 251 | out of memory | OUT OF MEMORY |
| 255 | `nitwit.sh`: `bin/nitwit32` is missing or not executable | ERROR |

## License

NITWIT is licensed under the New BSD license (BSD-3-Clause), see `LICENSE`. The executables contain third-party code,
see `THIRD-PARTY.md`.
