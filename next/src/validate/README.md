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
| `branching` | after the controlling expression | keyword (`if`, `while`, `for`, `switch`; for a do loop `do` or its `while`), or the `?` | the outcome is the constraint (`true`/`false`); for `switch`, the value is the case value (or `default`: no case label matches) |
| `function_enter` | after the arguments | the `)` of the call | always |
| `function_return` | after the call returned | the `)` of the call | the constraint (with `\result`) holds, or there is none |
| `target` | when the error function is called | the start of the call (the function's name); without a column the first statement or full expression in the line | the target segment is current |

- **Every waypoint but the target must be at an evaluation point of its type**, or the witness does not fit the
  program: the verdict is UNKNOWN (exit 4, Unsupported) before any run, naming the waypoint. A witness with
  inconsistent locations is invalid, and a waypoint that matched nothing would never be evaluated, which for an avoid
  waypoint would confirm what the witness excludes. This covers a line that does not exist, a column inside a
  construct, a column after a change by the preprocessor, a line without evaluation point of the type (for a waypoint
  without column), and a `location.function` that is not the function containing the evaluation point.
- **Without a column**, a waypoint is evaluated at the first evaluation point of its type in the line, by column.
  `validation-crafted` requires this: in `ternary.1B0`, two branching waypoints of line 15 without column both mean
  the first `?:` of the line, so the witness must be rejected. A target without a column is, as the format says, the
  first statement or full expression in the line, which the call of the error function must start: in
  `ERROR: reach_error();` that is the labeled statement, not the call. Only Paulina's crafted witnesses omit the
  target's column in SV-COMP 2026 (86 of 200), always at a line `reach_error();`.
- **Blocks** have no evaluation point of their own (the interpreter reports no event for them). An assumption at a `{`,
  where CPAchecker puts some, is evaluated before the first statement in the block, which runs in the same state. An
  empty block has no evaluation point, and neither has a block whose first statement has a label (`L:`, `case`,
  `default`, also the body of a `switch`): a jump enters it there without passing the `{`.
- **Labels:** the interpreter reports a statement event for a label (or `case`) and again for the statement it labels.
- **Preprocessed programs** (NITWIT ran `cpp`): lines are presumed lines of the original file. `cpp` expands macros,
  replaces comments by a blank and collapses runs of blanks, which moves the text after them. A column is therefore
  only used where the `cpp` line and the original line agree byte for byte up to and including it, a blank agreeing
  with any blank (`cpp` writes a tab of the indentation as a space). A column after a change, or beyond the end of the
  original line (into a macro expansion, as in `testfiles/sv-comp-2019/09.assert.c`), is at no evaluation point.
  Messages give original columns; a location after a change is given as "line (column c after preprocessing)". A
  `#line` directive in such a program makes the witness not fit: presumed lines would then differ from the lines the
  witness counts.
- **Branching values:** `true`, `false` and `default` in any case and with blanks around them; a case value is an
  integer constant expression. An outcome at a `switch`, or a case value elsewhere, is UNKNOWN.
- **Constraints** are compiled at their first evaluation and kept for later runs (so that the sites of input requests
  in them stay the same). Their scope is that of the event's location in the function of the current frame, which
  contains the location; `location.function` is only checked.

### Follow, avoid and progress

All waypoints are evaluated in the state of the run, on the values the inputs have in it. At an event, the follow
waypoint of the current segment is evaluated first, then its avoid waypoints there.

- **Follow:** a follow waypoint must pass at its first evaluation point. If it fails and its failure fixes inputs
  ("Values"), the run restarts; otherwise the execution has left the witness and is rejected (exit 241). A passing
  follow waypoint makes the next segment current.
- **Avoid:** an avoid waypoint of the current segment that passes rejects the execution (exit 241), also where the
  follow waypoint passed at the same event, unless fixing an input may make it fail ("Values"): then the run
  restarts, and the next run evaluates it again on the fixed input. An avoid constraint without value (undefined
  behaviour, an invalid read in a conjunct that `&&` evaluates) makes the verdict UNKNOWN (exit 4): whether it passes
  is not decided.
- **Cascading:** one event can complete several segments only through `assumption` waypoints at the same statement,
  and they all refer to the same execution of the statement: CPAchecker writes several identical assumptions, often
  `1`, at one location. After a follow assumption passed, the waypoints of the next segment are evaluated at the same
  event: its follow waypoint, if it is an assumption at this statement, must pass there, and its avoid waypoints there
  are evaluated.
- **When the avoid waypoints of a new segment start:** a segment that became current by a cascaded assumption
  evaluates its avoid waypoints at that event, for the same execution of the statement. A segment that became current
  by any other waypoint type (branching, function_enter, function_return) evaluates its waypoints from the next event
  on: one at the same `if`, loop or call refers to its next execution, as consecutive branchings of a loop do.
- **Verdict:**

  | Outcome | Exit code |
  |---|---|
  | the error function is called at the target while the target segment is current | 0 (confirmed) |
  | the error function is called earlier, or elsewhere | 252 |
  | the program ends (end of `main`, `exit`) | 242 |
  | the target segment is reached, but the program ends without calling the error function | 250 |
  | the program aborts, `__VERIFIER_assume` fails, undefined behaviour without a defined result, a limit | 4 |
  | a construct the interpreter does not support, a constraint that does not compile, an unsupported witness, a witness that does not fit the program, an avoid constraint without value | 4 |
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
- **Stage 7 replaces the fixes by the search below.**

### Search (stage 7, being implemented)

The witness says where the execution must go: at each waypoint, which outcome it needs. The run says how the outcome
depends on the inputs: values derived from inputs carry terms (`value/README.md`, "Terms"). When a waypoint does not
have the outcome it needs, the validator solves for inputs that give it, without running the program, and restarts
with them. It never tries values by running the program, so each restart costs one run and comes from one prediction.

- **Requirements:** what a waypoint evaluation needs, as clauses of atoms `term op constant`, every clause needing one
  atom that holds:

  | Waypoint | Requirement |
  |---|---|
  | follow `branching` at `if`, a loop or `?:`, outcome b | `t != 0` if b, else `t == 0`, for the condition's term `t` |
  | follow `branching` at `switch`, case value v | `t == v` |
  | follow `branching` at `switch`, `default` | `t != v` for every case value v, one clause each |
  | follow `assumption`, `function_return` | `c != 0` for each conjunct c, one clause each |
  | avoid `branching`, outcome b | `t == 0` if b, else `t != 0` |
  | avoid `branching` at `switch` | `t != v` for an avoided case value v; `t == v` for one of the case values if `default` is avoided |
  | avoid `assumption`, `function_return` | one clause: `c == 0` for one of the conjuncts c |

  A conjunct or condition without a term depends on no input the validator can change: if it does not hold, no input
  is changed for it. An atom whose term is the opaque term (`TermTable::isOpaque`: the value depends on inputs in a way
  the terms do not record) is unknown, and no input is changed for it either.
- **Kept clauses:** the requirements of every waypoint evaluation of the run that had its outcome (a follow waypoint
  that passed, an avoid waypoint that did not), as far as they have terms. New input values must keep them, so that
  the next run passes the same waypoints again.
- **Changeable inputs:** inputs of the run with the default value or a value fixed by an earlier run; not hinted
  inputs (the witness gave their value). Inputs with the default value are tried first, then fixed ones; within each
  group, later requests first.
- **Solving** a requirement that does not hold, greedily and without backtracking across clauses: each clause that
  does not hold gets its first atom that can be made to hold; the values chosen for earlier clauses stay.
  - An atom `t op c` is first decomposed: `t != 0` of `a && b` needs both, of `a || b` one of them (tried in order),
    of `!a` needs `a == 0`; `t == 0` the negations; a comparison `a op' b` whose value must be non-zero is the atom
    `a op' b`, with the side that has no changeable input evaluated as the constant.
  - **Inversion:** if a changeable input occurs once in the side that has changeable inputs, and the path to it has
    only invertible operations (conversions; `+`, `-`, `^` and `~` with the other operand fixed; unary `-`; `*` by an
    odd constant in an unsigned format), the value for the side is chosen as for hints (`values.hpp`, `choose`) and
    inverted down to the input.
  - **Candidates** otherwise, and for any input of the atom that inversion cannot reach: the input's run value ± 1,
    0, 1, −1, and each constant of the term ± 1, in this order, each checked by evaluating the term.
  - A value is taken only if, evaluated with all values chosen so far, it makes the atom hold, keeps every kept clause
    that mentions the input (a clause whose value becomes unknown does not count against it), and converts back
    unchanged from the input's type. Values an input was fixed to in earlier runs are tried after all others.
  - If every clause of the requirement then holds (or is unknown, because the run did not evaluate an operand), the
    chosen values become fixed inputs and the run restarts; otherwise the waypoint fails as before (exit 241).
- **Restarts** happen only when at least one input gets a new value. Fixed inputs may get another value in a later
  run when the search needs it and the kept clauses allow it. The number of restarts is limited
  (`ValidationOptions::maxRestarts`, scaled with the number of waypoints).
- **Limits of the search:** it cannot predict values through operations without term kinds (pointer arithmetic,
  library functions, bit-fields, dependencies through control flow that no waypoint covers), relations where an
  input occurs more than once and candidates do not help (`x * x == 49`), and witnesses without waypoints between
  the start and the target. Searching the program's paths for those would cost runs per candidate, which NITWIT does
  not do. An SMT solver can later replace the candidates behind the same requirements.

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
