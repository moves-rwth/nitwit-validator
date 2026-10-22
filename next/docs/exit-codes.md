# Exit codes

NITWIT 4 keeps the exit codes of NITWIT 3.x, because BenchExec's tool-info module `nitwit.py` maps them to results.

| Code | Meaning | BenchExec |
|---|---|---|
| 0 | witness confirmed: the error function was called and the witness was followed to its target | FALSE |
| 2 | the witness cannot be parsed | ERROR |
| 3 | wrong command line, or a file cannot be read | ERROR |
| 4 | UNKNOWN: unsupported program or witness feature, or an interpreter error | UNKNOWN |
| 5 | deprecated (3.x: the witness reached its target, but the error function was not called; now 250) | UNKNOWN |
| 240 | no witness given | ERROR |
| 241 | the witness was left: a follow waypoint failed, an avoid waypoint passed, or the automaton reached its sink | UNKNOWN |
| 242 | the program finished (end of `main` or `exit`) without calling the error function | UNKNOWN |
| 243 | the witness is in an illegal state | UNKNOWN |
| 245 | format 1.0 only, non-strict mode: the error function was called, but the automaton is not in a violation state | FALSE |
| 250 | the witness reached its target, but the error function was not called | UNKNOWN |
| 251 | out of memory | OUT OF MEMORY |
| 252 | format 2.x: the error function was called, but the witness was not followed to its target | UNKNOWN |
| 255 | wrapper script `nitwit.sh`: the executable is missing (3.x's also: unsupported property, missing files) | ERROR |

3.x also used 244, 246 to 249 for specific interpreter errors; NITWIT 4 reports those cases as 4 with a message.
NITWIT 4 adds no new codes until the BenchExec module knows them. NITWIT's own limits (restarts, call depth, the host
stack) end with 4 and a message naming the limit. NITWIT sets no time or step limit (`RunOptions::maxSteps` stays 0):
BenchExec's time limit ends a run that does not finish, and BenchExec reports it as TIMEOUT.

The result line printed before exiting starts with `VALIDATED:` (0), `FAILED:` (241, 242, 250, 252), `ERROR:` (2, 3)
or `UNKNOWN:` (4, 251).
