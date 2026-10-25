# Exit codes

NITWIT 4 keeps the exit codes of NITWIT 3.x, because BenchExec's tool-info module `nitwit.py` maps them to results.
The one exception: NITWIT 4 reports the case of 252 as 241 (see below the table).

| Code | Meaning | BenchExec |
|---|---|---|
| 0 | witness confirmed: the error function was called and the witness was followed to its target | FALSE |
| 2 | the witness cannot be parsed | ERROR |
| 3 | wrong command line, or a file cannot be read | ERROR |
| 4 | UNKNOWN: an unsupported program, witness or property, a witness that does not fit the program; the program aborts; an infeasible run (a failed `__VERIFIER_assume`, an endless loop without effect); undefined behaviour without a defined result; a limit of NITWIT (restarts, call depth, host stack) | UNKNOWN |
| 5 | not used (3.x: the witness reached its target, but the error function was not called; NITWIT 4 uses 250) | UNKNOWN |
| 240 | no witness given | ERROR |
| 241 | the witness was left: a follow waypoint failed or an avoid waypoint passed; also the case of 252 | UNKNOWN |
| 242 | the program finished (end of `main` or `exit`) without calling the error function | UNKNOWN |
| 243 | not used (3.x: the witness is in an illegal state) | UNKNOWN |
| 245 | not used (3.x, format 1.0, non-strict mode: the error function was called, but the automaton is not in a violation state) | FALSE |
| 250 | the witness reached its target, but the error function was not called | UNKNOWN |
| 251 | out of memory | OUT OF MEMORY |
| 252 | reserved, reported as 241: format 2.x, the error function was called, but the witness was not followed to its target | UNKNOWN in the maintainer's fork, ERROR in BenchExec 3.35 |
| 255 | wrapper script `nitwit.sh`: the executable is missing (3.x's also: unsupported property, missing files) | ERROR |

**252** is what 3.x exits with when the error function is called before the witness was followed to its target.
BenchExec 3.35 maps 252 to ERROR, so NITWIT 4 exits with 241 in that case, which every version of `nitwit.py` maps to
UNKNOWN; the result line (`FAILED: the target was not reached: ...`) still names the case. Once BenchExec maps 252 to
UNKNOWN, NITWIT 4 will use it (`docs/roadmap.md`).

3.x also used 244, 246 to 249 for specific interpreter errors; NITWIT 4 reports those cases as 4 with a message.
NITWIT 4 adds no codes that the BenchExec module does not know. NITWIT's own limits (restarts, call depth, the host
stack) end with 4 and a message naming the limit. NITWIT sets no time or step limit (`RunOptions::maxSteps` stays 0):
BenchExec's time limit ends a run that does not finish, and BenchExec reports it as TIMEOUT. There are two exceptions.
The retry with inputs 1 by default (`src/validate/README.md`, "Defaults") may take, in all its runs, the events of all
runs before and 2^22 more; beyond, it ends, and the verdict is that of the last run before it. And a run that enters an endless loop without
effect (`L: goto L;`, `while (1);`) ends after one round of it with 4, as an infeasible path (`src/validate/README.md`,
"Verdict").

The result line printed before exiting starts with `VALIDATED:` (0), `FAILED:` (241, 242, 250), `ERROR:` (2, and 3
for a file that cannot be read) or `UNKNOWN:` (4, 251). A wrong command line (3) and a missing witness (240) print no
result line, but the usage or "no witness given (-w)" on stderr.
