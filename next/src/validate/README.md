# Module `validate` (stages 6 and 7)

Validates a witness by running the program with an `ExecutionObserver` (the monitor) and an `InputOracle`, restarting
with inputs that the search predicts to give a waypoint its outcome. It decides the verdict and the exit code
(`docs/exit-codes.md`).

- `validator.hpp`: the API, `validate` and `exitCode`.
- `support_check.hpp`: the witness features NITWIT rejects before running ("Structure" below).
- `location.hpp`: witness locations as positions in the program, physical or presumed lines ("Evaluation points").
- `constraints.hpp`: constraints compiled at the event where they are evaluated, split into conjuncts; branching
  values.
- `monitor.hpp`: the segment monitor, the observer of a run ("Follow, avoid and progress").
- `oracle.hpp`: the input oracle: fixed inputs, hints, defaults ("Values").
- `search.hpp`: the search for inputs that give a waypoint the outcome it needs ("Search").
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
| `branching` | after the controlling expression | keyword (`if`, `while`, `for`, `switch`; for a do loop `do` or its `while`), or the `?` | the outcome is the constraint (`true`/`false`); for `switch`, the value is the case value (in a GNU range `case lo ... hi:`, any value of the range), or `default`: no case label matches |
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

- **Follow:** a follow waypoint must pass at its first evaluation point. If it fails and the search finds inputs with
  which it may pass ("Search"), the run restarts with them; otherwise the execution has left the witness and is
  rejected (exit 241). A passing follow waypoint makes the next segment current.
- **Avoid:** an avoid waypoint of the current segment that passes rejects the execution (exit 241), also where the
  follow waypoint passed at the same event, unless the search finds inputs with which it may fail ("Search"): then
  the run restarts with them, and the next run evaluates it again. An avoid constraint without value (undefined
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

An input is *fixed* by an earlier run (by the search, "Search" below), *hinted* by a function_return waypoint, or else
gets the *default*.

- **Hints:** a `function_return` waypoint of the current segment at a call whose result is an input. Its relations of
  `\result` with a constant (`==`, `!=`, `<`, `<=`, `>`, `>=`, either side) give the value of the call's input request.
  - A follow hint satisfies all such relations of the constraint; an avoid hint violates one of them. A follow hint
    wins over an avoid hint at the same call.
  - The value is chosen in the type of the comparison and must survive the conversion to the call's type and back
    (`\result >= 255U` for an `unsigned char` gives 255; `== 300` for a `char` gives no hint).
  - Hints are computed at the call event, where constants are evaluated in the current frame.
- **Every confirmation comes from one complete run** in which all follow waypoints passed and no avoid waypoint passed,
  on the inputs of that run. The search only chooses the inputs of the next run, so a prediction that does not hold
  (the program takes another path, a term is not what the program computes) costs a restart, never soundness.
- **Defaults:** `ValidationOptions::defaultInput`, 0, converted to the requested type; pointers get null. NITWIT 3.x
  effectively used 1; on the sample, 1 confirms one witness more, on `validation-crafted` six fewer.
- **Escapes:** producer escapes like `"\result"` in double quotes are repaired by the witness parser.

### Search

The witness says where the execution must go: at each waypoint, which outcome it needs. The run says how the outcome
depends on the inputs: values derived from inputs carry terms (`value/README.md`, "Terms"). When a waypoint does not
have the outcome it needs, the validator solves for inputs that give it, without running the program, and restarts
with them (`search.hpp`). It never tries values by running the program, so each restart costs one run and comes from
one prediction.

- **Requirements:** what a waypoint evaluation needs, as clauses of atoms `term op constant`, every clause needing one
  atom that holds; the constant is converted to the format of the term's value:

  | Waypoint | Requirement |
  |---|---|
  | follow `branching` at `if`, a loop or `?:`, outcome b | `t != 0` if b, else `t == 0`, for the condition's term `t` |
  | follow `branching` at `switch`, case value v | `t == v`; for a range `case lo ... hi:`, `t >= lo` and `t <= hi`, one clause each |
  | follow `branching` at `switch`, `default` | `t != v` for every case label v, one clause each; `t < lo \|\| t > hi` for a range |
  | follow `assumption`, `function_return` | `c != 0` for each conjunct c, one clause each |
  | avoid `branching`, outcome b | `t == 0` if b, else `t != 0` |
  | avoid `branching` at `switch`, case value v | `t != v`; for a range, `t < lo \|\| t > hi` |
  | avoid `branching` at `switch`, `default`, passing | one clause: `t == v` for one of the case labels v (the first value of a range) |
  | avoid `branching` at `switch`, `default`, not passing | the requirement of following the case label the run took |
  | avoid `assumption`, `function_return` | one clause: `c == 0` for one of the conjuncts c up to the first false one |

- **Unknown atoms:** an atom without a term (a conjunct or condition that depends on no input, or only through control
  flow, or has no value) and an atom on the opaque term (`TermTable::isOpaque`) are unknown. No input is changed for
  them, and they count against no value. A waypoint without a constraint (`function_enter`, a `function_return`
  without one) has no requirement: an avoid one that passes is rejected, logged as `needs another path, which the
  terms do not describe`, as is a requirement that holds on the values of the run.
- **Kept clauses:** the requirements of every waypoint evaluation of the run that had its outcome (a follow waypoint
  that passed, an avoid waypoint that did not), so that the next run passes the same waypoints again (`KeptClauses`).
  - A clause is kept once, and not at all without an atom on a term other than the opaque term: no input can make it
    false. A loop that passes one waypoint a million times on the same term keeps one clause.
  - The kept clauses are indexed by the inputs they depend on, so a value is checked against the clauses that
    mention its input only.
  - A run keeps clauses with at most 1,000,000 atoms in all (`ValidationOptions::maxKeptAtoms`): an atom takes 32
    bytes, and a clause about 50 more, so at most about 80 MB, for clauses of one atom each. Later clauses are not
    kept, which the log says once. Values may then break them, which costs a restart, never soundness.
- **Changeable inputs:** inputs of the run of arithmetic types with the default value or a value fixed by an earlier
  run; not hinted inputs (the witness gave their value). Inputs with the default value are tried first, then fixed
  ones; within each group, later requests first: a later input changes less of the path to the waypoint.
- **Solving** a requirement greedily over its clauses, in order, without backtracking across clauses. A clause that
  does not hold gets its first atom that can be made to hold; values chosen for earlier clauses may change for later
  ones only as far as the earlier clauses keep holding.
  - **Decomposition:** `t != 0` of `a && b` needs both (solving `b` keeps `a`), of `a || b` one of them, tried
    in order; `t == 0` the negations; `!a` and a conversion to `_Bool` pass the atom on to `a`. A comparison
    `a op' b` that must hold (or fail: `a op'' b` with the negated operator) is a relation of its two sides. An operand
    the run did not evaluate (`a && b` where `a` was false) is unknown: making `a` hold is all the search can do there.
    `c ? x : y` is solved through the operand the run evaluated, with `c` keeping its value, else by changing `c`: the
    other operand is unknown then, like a missing operand of `&&`. A goal that failed is not decomposed again for the
    same atom while the same guards hold (one that failed under a conjunct solved before may hold without it); this
    only saves cost on shared subterms (`c = c || c` in a loop), and a pass decomposes at most 20,000 goals
    (`SearchLimits`).
  - **Inversion:** if a changeable input occurs once in one side of the relation, the value for the side is chosen as
    for hints (`values.hpp`, `choose`: `x < c` gives `c - 1`, `x != c` gives 0, or 1 if `c` is 0) with the other side's
    value as the constant, and inverted down the path to the input, the other operand of each operation keeping its
    value:
    - conversions, where the value converts back unchanged (`(int)c == 300` has no `char` c);
    - integer `+`, `-` (either operand), `^`, `~`, unary `-`, wrapping around as the run does;
    - integer `*` by a factor that divides the value, and in an unsigned format by a factor 2^k · f with f odd where
      the value has k trailing zero bits: the value shifted right by k times the inverse of f modulo 2^bits;
    - integer `/` of the dividend by a non-zero divisor: the value times the divisor;
    - floating `+`, `-` (either operand) and unary `-`.

    The relation is then evaluated with the inverted value, which rounding or a truncating division may make miss.
  - **Candidates** follow, for every input of the relation, also those that inversion cannot reach: the input's run
    value + 1 and − 1, 0, 1, −1, then the constant of the atom and the constants of the terms, each with its
    neighbours − 1 and + 1. A clause that blocks a value adds its constants and their neighbours. The non-negative
    candidates come before the negative ones, each in this order: programs often reject negative inputs. At most 8
    inputs per relation, and 8 constants of the relation and 8 of blocking clauses per input, are tried.
  - **A value is taken** only if, evaluated with all values chosen so far, it is a value of the input's type (a
    `_Bool` is 0 or 1), makes the relation hold, makes no kept clause and no earlier clause of the requirement that
    mentions the input false, and makes neither them nor the clause being solved end the run (`NoValue::EndsRun`:
    division by zero, any undefined behaviour in the strict terms of a constraint). A clause whose value becomes
    unknown does not count against a value.
  - **Repair:** if the only obstacle of a value is one kept clause that it makes false, the clause is solved like a
    requirement through its other changeable inputs, with the value's input pinned and its relation kept: `x + y == 10`,
    kept with y = 10, and then `y == 3` give y := 3 and x := 7. One level only: a repair does not repair in turn.
  - **Values of earlier runs:** the whole requirement is first solved without any value an input was fixed to in an
    earlier run, so that two waypoints do not take turns changing one input; only if that fails are those values
    allowed, after all others.
  - **Cost:** each value tried is evaluated against the relation and the clauses that mention its input, and a
    repair solves a clause for each value it repairs, so the kept clauses of an input and repairs multiply the work.
    A search therefore evaluates at most 300,000 terms, both passes and repairs included, well under a second; beyond
    that it ends without values, and the log says that it stopped (`SearchLimits`).
  - If every clause of the requirement then holds or is unknown, and at least one input has a new value, the new
    values become fixed inputs and the run restarts (`fixing input 3 (...) to 5 (inversion)` in the log); otherwise
    the waypoint fails as before (exit 241). The log (`-v`) says what the waypoint needed and what was chosen, e.g.
    ``run 3: the follow waypoint of segment 5 (branching at 12:3) needs `in7 != 0`; input 7 := 1 (inversion)``.
- **Switches:** a case value of a follow waypoint gives the switched value that value (any value of a range). Its
  `default` excludes every case label, which gives the smallest non-negative value that is no case value where the
  run's value is a case value: the candidates put the case values' neighbours before negative values. An avoid
  waypoint at `default` that passes gets the first case label that the kept clauses allow, so that the avoid waypoints
  evaluated before it at the `switch` keep failing; one at a case value gets another value (0, or 1 for the case 0,
  else the next value no kept clause excludes), and one at a range `lo ... hi` leaves it, to `lo - 1` if that is not
  negative, else to `hi + 1`, as the candidates prefer non-negative values. An avoid waypoint after it in the segment
  that the new value makes pass costs another restart (`validation-crafted/switch.3A1` avoids two cases and `default`
  at one `switch`).
- **Restarts** happen only when at least one input gets a new value. Fixed inputs may get another value in a later
  run when the search needs it and the kept clauses allow it.
  - Runs are deterministic, and so is the search, which depends on the run and on the values each input was fixed
    to before. The values fixed before only grow, so the state that can repeat is the fixed inputs together with the
    number of distinct values fixed before. If the search proposes the fixed inputs of an earlier run and no input
    was fixed to a new value since that run, the next run and its search would repeat it: the execution is rejected
    (exit 241), with "the search proposes the inputs of run n again". With a new value fixed since, the same inputs
    may lead elsewhere: the search after the repeated run knows more values to avoid. The oracle finds such a run by
    a fingerprint of the fixed values (the sum of their hashes) and the number of distinct values, and compares the
    values exactly where both agree.
  - The limit is `min(ValidationOptions::maxRestarts, 16 + 2 · waypoints)` (`restartLimit`). A waypoint can need a
    restart for each prediction that fails, and a loop counter one per iteration; 16 cover witnesses with few
    waypoints. The cap of 10,000 bounds long witnesses, whose runs would exceed the time limit before.
- **Examples:**
  - `if (tmp)` after `tmp = __VERIFIER_nondet_int()`, which the witness wants true: `in0 != 0` gives tmp := 1.
  - The input check of RERS programs, `if ((input != 1) && (input != 2) && ...) return -2;`, which the witness wants
    false, followed by `if (input == 3)`, which it wants true: the first run gives input := 1 (the first conjunct made
    false), the second input := 3. The kept check is unknown with 3, since the second run did not evaluate
    `input != 2`, so it does not count against 3, and the third run passes both.
  - A loop counter, `while (i <= a) i++;` with a = 100, that the witness passes 33 times: the run keeps `0 <= a` to
    `32 <= a` and then needs `!(33 <= a)`, so a := 32. With a = 0 instead, each iteration needs a restart.
- **Limits of the search:** it cannot predict values through operations without term kinds (pointer arithmetic,
  library functions, bit-fields, dependencies through control flow that no waypoint covers), relations where an
  input occurs more than once and candidates do not help (`x * x == 49`), and witnesses without waypoints between
  the start and the target. Searching the program's paths for those would cost runs per candidate, which NITWIT does
  not do. An SMT solver can later replace the candidates behind the same requirements. An avoid assumption evaluated
  in a hot loop records the strict terms of its constraint at each evaluation and can fill the term table, after which
  later values get the opaque term.

## Known gaps

- **Values the search does not find** ("Limits of the search"); on the sample, no witness reaches a limit of the
  search (goals, evaluations, restarts, kept atoms), and with all of them raised a hundredfold every witness ends as
  before. The waypoints that fail there need:
  - values that depend on inputs only through control flow, or through memory, pointers or library functions that
    terms do not follow: requirements without a term or on the opaque term (the `aws` `byte_buf` programs, the
    `v_FIFO` of btor2c);
  - a relation that no inversion and no candidate satisfies: overflow checks with an input on both sides
    (`(x + 10) < x` needs a value near the maximum of the type), constraints over `&` and `|` of truth values (the
    xcsp programs encode SAT problems), polynomials (`bresenham`), `?:` over `signed char` (pals).
- **Paths between waypoints:** the search knows the requirements of waypoints only. Where values change the path at a
  condition no waypoint covers, the run leaves the witness without a failing waypoint: an `assume_abort_if_not` aborts
  or a null pointer is read (exit 4), or the program ends before the target segment or in it without calling the error
  function (exit 242, 250). Keeping the conditions of all branchings of a run, or a concolic search with an SMT solver
  (`docs/plan.md`, "later"), would cover them.
- **Unsupported constructs** of the interpreter end a run with exit 4: threads (`pthread_create`) and `setjmp`.
- Long runs can exhaust the host's memory before the interpreter's memory limit, probably through the memory model's
  growing table of dead heap allocations; the driver then reports exit code 251.

## Tests

- `tests/unit/validate/`: each rule above, with small programs, witnesses built in code, and scripted runs
  (`validate_test_support.hpp`, whose `FakeExecution` records terms as the interpreter does);
  `end_to_end_test.cpp` with the interpreter and the terms it records, so with the search: a constraint on a computed
  value, a loop counter, the input check of RERS, the result of a defined function, avoided switch cases and narrowing
  conversions; `search_test.cpp` the search on term tables built by hand, one test per rule (inversion through every
  invertible operation, candidates and their order, kept clauses and their index, repairs, unknown atoms, `&&`, `||`
  and `?:`, shared subterms, switches, avoid waypoints, the order of inputs and values, the two passes, the memo under
  guards, the budgets of goals and evaluations, the explanation) and the examples above (the input check of RERS, a
  loop counter, `if (tmp)`); monitor tests of the search over several runs: an input that two waypoints would change
  back and forth, a run that would repeat an earlier one and one that repeats its inputs with a value tried since,
  case ranges, avoided cases of a switch, a repaired clause, the limit of kept clauses.
- `tests/corpus/validate/`, label `corpus`: runs `nitwit` per witness and checks the exit criteria of stage 6 (which
  stage 7 keeps: 0 wrong confirmations, at least the results of 3.x) on
  - `testfiles/yaml-witnesses` and the 2.x witnesses in `testfiles/sv-comp-2019`;
  - `validation-crafted` (expected results in `*.witness-validation.yml`);
  - the 250-witness sample, compared per witness with the 3.x results in `results/2026-10-20-nitwit-strict-v3-*`.
