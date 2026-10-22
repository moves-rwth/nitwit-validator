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
confirmed), made precise where the test witnesses and `validation-crafted` required it. Where 3.x confirmed invalid
witnesses, NITWIT 4 deviates: avoid waypoints are evaluated on the inputs of the run, cascaded waypoints refer to the
same execution, and columns of preprocessed programs must point to unchanged text. The format's own description is in
sv-witnesses (`README.md`, "violation witnesses").

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
  - a constraint on a `function_enter` or `target` waypoint, which the format does not have (no SV-COMP 2026
    unreach-call witness writes one);
  - a waypoint whose `location.file_name` names another file than the program, compared by the last path component.
    Its line would be matched against the program's lines. All 13 million waypoints of the SV-COMP 2026 unreach-call
    witnesses name the program;
  - no or more than one `violation_sequence`.

### Evaluation points

Only the waypoints of the current segment are evaluated. A location is a line and, if given, a column; both count bytes
from 1. A column 0 or beyond the end of the line (one past its text still counts) matches nothing.

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
- **Preprocessed programs** (NITWIT ran `cpp`): lines are presumed lines of the original file. `cpp` expands macros,
  replaces comments by a blank and collapses runs of blanks, which moves the text after them. A column is therefore
  only used where the `cpp` line and the original line agree byte for byte up to and including it, a blank agreeing
  with any blank (`cpp` writes a tab of the indentation as a space). A column after a change matches nothing in that
  line (the verbose log says so), and so does a column beyond the end of the original line (into a macro expansion,
  as in `testfiles/sv-comp-2019/09.assert.c`). Messages give original columns; a location after a change is given as
  "line (column c after preprocessing)".
- **Branching values:** `true`, `false` and `default` in any case and with blanks around them; a case value is an
  integer constant expression. An outcome at a `switch`, or a case value elsewhere, is UNKNOWN.
- **Constraints** are compiled at their first evaluation and kept for later runs (so that the sites of input requests
  in them stay the same). Their scope is that of the event's location in the function of the current frame, which
  contains the location; `location.function` is not needed.

### Follow, avoid and progress

All waypoints are evaluated in the state of the run, on the values the inputs have in it. At an event, the follow
waypoint of the current segment is evaluated first, then its avoid waypoints there.

- **Follow:** a follow waypoint must pass at its first evaluation point. If it fails and its failure fixes inputs
  ("Values"), the run restarts; otherwise the execution has left the witness and is rejected (exit 241). A passing
  follow waypoint makes the next segment current.
- **Avoid:** an avoid waypoint of the current segment that passes rejects the execution (exit 241), also where the
  follow waypoint passed at the same event, unless fixing an input may make it fail ("Values"): then the run
  restarts, and the next run evaluates it again on the fixed input.
- **Cascading:** one event can complete several segments only through `assumption` waypoints at the same statement,
  and they all refer to the same execution of the statement: CPAchecker writes several identical assumptions, often
  `1`, at one location. After a follow assumption passed, the waypoints of the next segment are evaluated at the same
  event: its follow waypoint, if it is an assumption at this statement, must pass there, and its avoid waypoints there
  are evaluated. Other waypoint types do not cascade: a waypoint of the next segment at the same `if` or loop refers to
  its next execution.
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
- **Fixes of follow waypoints:** a conjunct `x op c` of a follow `assumption` or `function_return` constraint that
  fails, where `x`'s value has an unfixed input as its origin, fixes that input and restarts the run.
  - `x` is taken without the conversions sema inserts for the comparison (promotions, usual arithmetic conversions).
    The value is chosen for `x` as for hints (`x < c` gives `c - 1`, `x != c` gives 0, or 1 if `c` is 0), then
    converted to the input's type, from which it must convert back to the same value.
  - A conjunct that is no relation with a constant (`flag`, `(char)x`) counts as `conjunct != 0` when its own value has
    an unfixed input as its origin.
  - The case value of a follow `branching` waypoint at a `switch` fixes the switched value in the same way, as 3.x
    did by turning it into the assumption `condition == value`.
  - `&&`-conjunctions are split. The failing conjuncts of one constraint fix all their inputs before one restart.
- **Fixes of avoid waypoints:** a passing avoid constraint fixes the input of its first conjunct that a fix may make
  false, in the same way: `x op c` gets a value for the negated relation (`x == c` gives 0, or 1 if `c` is 0), another
  conjunct the value 0. At a `switch`, a passing avoid waypoint fixes the switched value to the first value that no
  avoid waypoint of the segment at this `switch` avoids: a case value in source order, else (unless `default` is
  avoided) the smallest non-negative value that matches no case label. `validation-crafted/switch.3A1` avoids two cases
  and `default` at one `switch`. Outcomes of other branchings are taken as they are (stage 7).
- **Restarts** are only made when at least one input gets fixed, and fixed inputs stay fixed, so every run fixes more
  inputs than the one before. The number of restarts is limited (`ValidationOptions::maxRestarts`).
- **Every confirmation comes from one complete run** in which all follow waypoints passed and no avoid waypoint passed,
  on the inputs of that run. A fix only chooses the inputs of the next run, so a fix that does not have the intended
  effect (an origin through type punning, `interp/README.md`, "Origins") costs a restart, never soundness.
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
