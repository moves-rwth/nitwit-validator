# Module `validate` (stage 6, API to be defined)

Validates a witness by running the program with an `ExecutionObserver` (the monitor) and an `InputOracle`, restarting
with other inputs where that can help. It decides the verdict and the exit code (`docs/exit-codes.md`).

## Semantics of witness format 2.x

These are the rules NITWIT 3.x implements after the P1/P2 fixes, measured on `validation-crafted` (77/100, no invalid
witness confirmed). The format's own description is in sv-witnesses (`README.md`, "violation witnesses").

### Structure

- A witness has one `violation_sequence`, a list of segments.
- Each segment has exactly one `follow` waypoint, which is its last waypoint, and any number of `avoid` waypoints
  before it.
- The `target` waypoint is the `follow` waypoint of the last segment.
- Rejected as UNKNOWN (exit 4):
  - `cycle` (non-termination);
  - `thread_id` other than 0;
  - a target outside the last segment, or a segment without a follow waypoint;
  - constraint formats other than `c_expression`;
  - more than one `violation_sequence`.

### Evaluation points

Only the waypoints of the current segment are evaluated. The location is the line plus, if given, the column. Without
a column, every candidate in the line matches.

| Type | Evaluated | Location | Passes when |
|---|---|---|---|
| `assumption` | before the statement | first character of the statement | the constraint holds |
| `branching` | after the controlling expression | keyword (`if`, `while`, `for`, `do`'s `while`, `switch`), or the `?` | the outcome is the constraint (`true`/`false`); for `switch`, the value matches the case label (or `default`: no case label matches) |
| `function_enter` | after the arguments | the `)` of the call | always |
| `function_return` | after the call returned | the `)` of the call | the constraint (with `\result`) holds, or there is none |
| `target` | never | | the error function is called while the target segment is current |

### Follow, avoid and progress

- **Follow:** a follow waypoint must pass at its first evaluation point. If it does not, the execution has left the
  witness and is rejected (exit 241). A passing follow waypoint makes the next segment current.
- **Avoid:** avoid waypoints of the current segment are evaluated at their evaluation points. If one passes, the
  execution is rejected. At an event where both a follow and an avoid waypoint are evaluated, a passing avoid
  waypoint wins. Avoid constraints never fix inputs: an input that is not fixed yet counts as "not passing".
- **Cascading:** one event can complete several segments only through `assumption` waypoints at the same statement.
  CPAchecker writes several identical assumptions, often `1`, at one location. Each of them passes non-strictly.
- **Verdict:**

  | Outcome | Exit code |
  |---|---|
  | the error function is called while the target segment is current | 0 (confirmed) |
  | the error function is called earlier | 252 |
  | the program ends (end of `main`, `exit`) | 242 |
  | the program aborts | 4 |
  | the target segment is reached, but the error function is never called | 250 |

### Values

- **`function_return` waypoints with `\result == c`** (or `!=`, `<`, `<=`, `>`, `>=` with a constant) on a
  nondeterministic function are *hints*. When the call site is reached in the current segment, the oracle returns a
  value that satisfies the constraint. For avoid waypoints, the oracle returns one that does not.
- **`assumption` and `function_return` constraints:**
  - A constraint of the form `x == c` (or the other relations) on an lvalue whose value is an unmodified copy of an
    unfixed input fixes that input and restarts the run.
  - `&&`-conjunctions are split; each part is handled alone.
  - Inputs fixed in this way stay fixed for later runs.
  - The number of restarts is limited.
- **Escapes:** producer escapes like `"\result"` in double quotes are repaired by the witness parser.
- **Stage 7 adds value search for `branching` waypoints:**
  - a condition that is an unmodified input (`if (__VERIFIER_nondet_int())`);
  - relations between such an input and a constant;
  - then a general search over unfixed inputs when a follow waypoint fails.
  Every confirmation still comes from one concrete run.

## Tests

- `tests/unit/validate/`: each rule above, with small programs and witnesses.
- `tests/corpus/validate/`, label `corpus`:
  - `testfiles/yaml-witnesses` and the 2.x witnesses in `testfiles/sv-comp-2019`;
  - `validation-crafted` (expected results in `*.witness-validation.yml`);
  - the 250-witness sample, compared per witness with the 3.x results in
    `results/2026-10-20-nitwit-strict-v3-*`.
