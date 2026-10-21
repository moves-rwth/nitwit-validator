# Module `validate` (stage 6)

Validates a witness by running the program with an `ExecutionObserver` (the monitor) and an `InputOracle`, restarting
with other inputs where that can help. It decides the verdict and the exit code (`docs/exit-codes.md`).

- `validator.hpp`: the API, `validate` and `exitCode`.
- `support_check.hpp`: the witness features NITWIT rejects before running ("Structure" below).
- `location.hpp`: witness locations as positions in the program, physical or presumed lines ("Evaluation points").
- `constraints.hpp`: constraints compiled at the event where they are evaluated, split into conjuncts; branching
  values.
- `monitor.hpp`: the segment monitor, the observer of a run ("Follow, avoid and progress").
- `oracle.hpp`: the input oracle: fixed inputs, hints, defaults ("Values").
- `values.hpp`: the conversions and the choice of values that satisfy a relation.
- `restart.hpp`: the restart loop, with the run as a parameter, so that tests can script runs.
- `verdict.hpp`: the verdict of the final run.

## Semantics of witness format 2.x

The rules of NITWIT 3.x after its P1/P2 fixes (77 of 100 right on `validation-crafted`, no invalid witness
confirmed), made precise where the test witnesses and `validation-crafted` required it. The format's own description is
in sv-witnesses (`README.md`, "violation witnesses").

### Structure

- A witness has one `violation_sequence`, a list of segments.
- Each segment has exactly one `follow` waypoint, which is its last waypoint, and any number of `avoid` waypoints
  before it.
- The `target` waypoint is the `follow` waypoint of the last segment.
- Rejected as UNKNOWN (exit 4):
  - `cycle` (non-termination);
  - `thread_id` other than 0;
  - a target outside the last segment, a target with action `avoid`, or a segment without exactly one follow
    waypoint at its end;
  - constraint formats other than none, `c_expression`, `acsl_expression` and `ext_c_expression`. Constraints of these
    formats are parsed as C plus `\result`; one that is not C makes the verdict UNKNOWN when it is compiled;
  - no or more than one `violation_sequence`.

### Evaluation points

Only the waypoints of the current segment are evaluated. A location is a line and, if given, a column; both count bytes
from 1.

| Type | Evaluated | Location | Passes when |
|---|---|---|---|
| `assumption` | before the statement | first character of the statement | the constraint holds |
| `branching` | after the controlling expression | keyword (`if`, `while`, `for`, `do`'s `while`, `switch`), or the `?` | the outcome is the constraint (`true`/`false`); for `switch`, the value is the case value (or `default`: no case label matches) |
| `function_enter` | after the arguments | the `)` of the call | always |
| `function_return` | after the call returned | the `)` of the call | the constraint (with `\result`) holds, or there is none |
| `target` | when the error function is called | the start of the call (the function's name) | the target segment is current |

- **Without a column**, a waypoint is evaluated at the first evaluation point of its type in the line, by column; a
  target matches every call in the line. `validation-crafted` requires this: in `ternary.1B0`, two branching waypoints
  of line 15 without column both mean the first `?:` of the line, so the witness must be rejected.
- **Blocks** have no evaluation point of their own (the interpreter reports no event for them). An assumption at a `{`,
  where CPAchecker puts some, is evaluated before the first statement in the block, which runs in the same state.
- **Labels:** the interpreter reports a statement event for a label (or `case`) and again for the statement it labels.
- **Preprocessed programs** (NITWIT ran `cpp`): lines are presumed lines of the original file, and a column beyond the
  end of the original line matches nothing (it points into a macro expansion, as in `testfiles/sv-comp-2019/09.assert.c`).
- **Branching values:** `true`, `false` and `default` in any case and with blanks around them; a case value is an
  integer constant expression. An outcome at a `switch`, or a case value elsewhere, is UNKNOWN.
- **Constraints** are compiled at their first evaluation and kept for later runs (so that the sites of input requests
  in them stay the same). Their scope is that of the event's location in the function of the current frame, which
  contains the location; `location.function` is not needed.

### Follow, avoid and progress

- **Follow:** a follow waypoint must pass at its first evaluation point. If it does not, the execution has left the
  witness and is rejected (exit 241). A passing follow waypoint makes the next segment current.
- **Avoid:** avoid waypoints of the current segment are evaluated at their evaluation points. If one passes, the
  execution is rejected. At an event where both a follow and an avoid waypoint are evaluated, a passing avoid
  waypoint wins. Avoid constraints never fix inputs: an input that is not fixed yet counts as "not passing". That is
  decided per conjunct: one whose value, or the subject of whose relation with a constant, has an input as its origin
  that is neither fixed nor hinted does not hold. For a `switch` case or `default`, the switched value decides.
  Outcomes of other branchings are taken as they are.
- **Cascading:** one event can complete several segments only through `assumption` waypoints at the same statement.
  CPAchecker writes several identical assumptions, often `1`, at one location. Each of them passes non-strictly: a
  cascaded assumption that fails is evaluated again at the next execution of the statement (unless its failure fixes
  inputs, which restarts the run). As in 3.x, the avoid waypoints of a segment are not evaluated at the event that
  made the segment current.
- **Verdict:**

  | Outcome | Exit code |
  |---|---|
  | the error function is called at the target while the target segment is current | 0 (confirmed) |
  | the error function is called earlier, or elsewhere | 252 |
  | the program ends (end of `main`, `exit`) | 242 |
  | the target segment is reached, but the program ends without calling the error function | 250 |
  | the program aborts, `__VERIFIER_assume` fails, undefined behaviour without a defined result, a limit | 4 |
  | a construct the interpreter does not support, a constraint that does not compile, an unsupported witness | 4 |
  | the program exceeds the memory limit | 251 |

### Values

An input is *fixed* by an earlier run, *hinted* by a function_return waypoint, or else gets the *default*. The origin
of a value is the input it is derived from only through copies and conversions (`interp/README.md`, "Origins").

- **Hints:** a `function_return` waypoint of the current segment at a call whose result is an input. Its relations of
  `\result` with a constant (`==`, `!=`, `<`, `<=`, `>`, `>=`, either side) give the value of the call's input request.
  - A follow hint satisfies all such relations of the constraint; an avoid hint violates one of them. A follow hint
    wins over an avoid hint at the same call.
  - The value is chosen in the type of the comparison and must survive the conversion to the call's type and back
    (`\result >= 255U` for an `unsigned char` gives 255; `== 300` for a `char` gives no hint).
  - Hints are computed at the call event, where constants are evaluated in the current frame.
- **Fixes:** a conjunct `x op c` of a follow `assumption` or `function_return` constraint that fails, where `x`'s
  value has an unfixed input as its origin, fixes that input and restarts the run.
  - `x` is taken without the conversions sema inserts for the comparison (promotions, usual arithmetic conversions).
    The value is chosen for `x` as for hints (`x < c` gives `c - 1`, `x != c` gives 0, or 1 if `c` is 0), then
    converted to the input's type, from which it must convert back to the same value.
  - The case value of a follow `branching` waypoint at a `switch` fixes the switched value in the same way, as 3.x
    did by turning it into the assumption `condition == value`.
  - `&&`-conjunctions are split. The failing conjuncts of one constraint fix all their inputs before one restart.
  - A restart is only made when at least one input gets fixed, and fixed inputs stay fixed, so every run fixes more
    inputs than the one before. The number of restarts is limited (`ValidationOptions::maxRestarts`).
- **Defaults:** `ValidationOptions::defaultInput`, 0, converted to the requested type; pointers get null. NITWIT 3.x
  effectively used 1; on the sample, 1 confirms one witness more, on `validation-crafted` six fewer.
- **Escapes:** producer escapes like `"\result"` in double quotes are repaired by the witness parser.
- **Stage 7 adds value search for `branching` waypoints:**
  - a condition that is an input (`if (__VERIFIER_nondet_int())`);
  - relations between such an input and a constant;
  - then a general search over unfixed inputs when a follow waypoint fails.
  Every confirmation still comes from one concrete run.

## Known gaps

- Witnesses without values (most of Automizer, Taipan, Kojak, Goblint and Mopsa) need the value search of stage 7, and
  so do constraints on computed values (`a == 10` after `a = a - n`, `validation-crafted/while.3A1`).
- Long runs can exhaust the host's memory before the interpreter's memory limit, probably through the memory model's
  growing table of dead heap allocations; the driver then reports exit code 251.

## Tests

- `tests/unit/validate/`: each rule above, with small programs, witnesses built in code, and scripted runs
  (`validate_test_support.hpp`); `end_to_end_test.cpp` with the interpreter.
- `tests/corpus/validate/`, label `corpus`: runs `nitwit` per witness and checks the stage's exit criteria on
  - `testfiles/yaml-witnesses` and the 2.x witnesses in `testfiles/sv-comp-2019`;
  - `validation-crafted` (expected results in `*.witness-validation.yml`);
  - the 250-witness sample, compared per witness with the 3.x results in `results/2026-10-20-nitwit-strict-v3-*`.
