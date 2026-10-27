# Module `validate`

Validates a witness by running the program with an `ExecutionObserver` (the monitor) and an `InputOracle`, restarting
with inputs that the search predicts to give a waypoint its outcome, or to keep a guard of the program from ending the
run. It decides the verdict and the exit code (`docs/exit-codes.md`).

- `validator.hpp`: the API, `validate` and `exitCode`.
- `support_check.hpp`: the witness features NITWIT rejects before running ("Structure" below).
- `location.hpp`: witness locations as positions in the program, physical or presumed lines ("Evaluation points").
- `constraints.hpp`: constraints compiled at the event where they are evaluated, split into conjuncts; branching
  values.
- `monitor.hpp`: the segment monitor, the observer of a run ("Follow, avoid and progress").
- `oracle.hpp`: the input oracle: fixed inputs, hints, defaults ("Values").
- `search.hpp`: the search for inputs that give a waypoint the outcome it needs ("Search").
- `guard.hpp`: the guards of the program, its `abort` branches and `__VERIFIER_assume` ("Guards").
- `values.hpp`: the conversions and the choice of values that satisfy a relation.
- `restart.hpp`: the restart loop and its two phases, with the run as a parameter, so that tests can script runs.
- `verdict.hpp`: the verdict of the final run.
- `concrete.hpp`: the confirming run written as a concrete witness ("Concrete witnesses").

## Semantics of witness format 2.x

The format's own description is in sv-witnesses (`README.md`, "violation witnesses"). The rules below make it
precise where the test witnesses and `validation-crafted` require it. They differ from those of NITWIT 3.x where 3.x
confirms invalid witnesses: avoid waypoints are evaluated on the inputs of the run, cascaded waypoints refer to the
same execution, and columns of preprocessed programs must point to unchanged text.

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
| `target` | when the error function is called | the first character of the innermost statement or full expression that contains the call, as the format says, or the start of the call (the function's name), where producers put it; without a column the first statement or full expression in the line | the target segment is current |

- **Every waypoint but the target must be at an evaluation point of its type**, or the witness does not fit the
  program: the verdict is UNKNOWN (exit 4, Unsupported) before any run, naming the waypoint. A witness with
  inconsistent locations is invalid, and a waypoint that matched nothing would never be evaluated, which for an avoid
  waypoint would confirm what the witness excludes. This covers a line that does not exist, a column inside a
  construct, a column after a change by the preprocessor, a line without evaluation point of the type (for a waypoint
  without column), and a `location.function` that is not the function containing the evaluation point.
- **Without a column**, a waypoint is evaluated at the first evaluation point of its type in the line, by column.
  `validation-crafted` requires this: in `ternary.1B0`, two branching waypoints of line 15 without column both mean
  the first `?:` of the line, so the witness must be rejected. A target without a column is, as the format says, the
  first statement or full expression in the line, which the call of the error function must start or be innermost in:
  in `ERROR: reach_error();` that is the labeled statement, which contains the innermost statement `reach_error();`
  and is therefore not the target, and in `int a = c ? (reach_error(), 0) : 1;` it is the declaration, which contains
  the innermost full expression, the initializer. Only Paulina's crafted witnesses omit the target's column in SV-COMP
  2026 (86 of 200), always at a line `reach_error();`.
- **Blocks** have no evaluation point of their own (the interpreter reports no event for them). An assumption at a `{`,
  where CPAchecker puts some, is evaluated before the first statement in the block, which runs in the same state. An
  empty block has no evaluation point, and neither has a block whose first statement has a label (`L:`, `case`,
  `default`, also the body of a `switch`): a jump enters it there without passing the `{`.
- **Labels:** the interpreter reports a statement event for a label (or `case`) and again for the statement it labels.
- **Non-local jumps** (`interp/README.md`, "Non-local jumps"): a function that a longjmp leaves does not return, so a
  `function_return` waypoint at its call is not evaluated there. The setjmp call returns again, with longjmp's value:
  the `function_return` waypoints at it and the branchings of the rest of its statement are evaluated again, its
  assumptions not, since the statement does not start again.
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
  - The format does not say whether consecutive assumptions at one statement refer to one execution or to successive
    ones. NITWIT reads them as one, which the first-evaluation rule implies, and rejects witnesses whose assumptions at
    one statement contradict each other within one execution. CPAchecker also writes one segment per call of a function
    with an assumption at its `{` (`choice == 0`, then `choice == 2`); those witnesses are not confirmed (1 of 1000 in
    the evaluation, `docs/evaluation.md`). Reading such an assumption as the statement's next execution when it cannot
    hold at this one confirms that witness, but also witnesses that contradict themselves within one execution, so
    NITWIT uses the strict reading. Identical per-call values (`2`, `2`, `2`) cannot be told apart from repeated
    assumptions about one execution in any reading.
- **When the avoid waypoints of a new segment start:** a segment that became current by a cascaded assumption
  evaluates its avoid waypoints at that event, for the same execution of the statement. A segment that became current
  by any other waypoint type (branching, function_enter, function_return) evaluates its waypoints from the next event
  on: one at the same `if`, loop or call refers to its next execution, as consecutive branchings of a loop do.
- **Verdict:**

  | Outcome | Exit code |
  |---|---|
  | the error function is called at the target while the target segment is current | 0 (confirmed) |
  | the error function is called earlier, or elsewhere | 241 (252 once BenchExec maps it to UNKNOWN, `docs/exit-codes.md`) |
  | the program ends (end of `main`, `exit`) | 242 |
  | the target segment is reached, but the program ends without calling the error function | 250 |
  | the program aborts, `__VERIFIER_assume` fails (infeasible), the program enters an endless loop without effect (infeasible), undefined behaviour without a defined result, a limit | 4 |
  | a construct the interpreter does not support, a constraint that does not compile, an unsupported witness, a witness that does not fit the program, an avoid constraint without value | 4 |
  | the program exceeds the memory limit | 251 |

- **Endless loops without effect** (`interp/README.md`, "Execution: behaviour to keep"): a run that enters a loop that
  never leaves and changes nothing, like `ldv_assume_label: goto ldv_assume_label;`, with which the
  ldv-linux-4.0-rc1-mav programs assume a false condition, would run for ever without calling the error function. It
  ends after one round of the loop (`RunOptions::endsEndlessLoops`). Within a segment, every round is identical: the
  program's state does not change, so the monitor evaluates the same waypoints with the same outcome. The segment
  changes in the loop only when a follow waypoint there passes, and after that the target cannot be reached, since the
  run never leaves the loop. So no confirmation is lost; what ending the run may change is a verdict that a later
  round would have given otherwise, e.g. 241 (a follow waypoint in the loop that fails in a later segment) into 4. The
  verdict is UNKNOWN (exit 4), as for a failed `__VERIFIER_assume`.

### Values

An input is *fixed* by an earlier run (by the search, "Search" below), *hinted* by a function_return waypoint, or else
gets the *default*.

- **Hints:** a `function_return` waypoint of the current segment at a call whose result is an input. Its relations of
  `\result` with a constant (`==`, `!=`, `<`, `<=`, `>`, `>=`, either side) give the value of the call's input request.
  - A follow hint satisfies all such relations of the constraint; an avoid hint violates one of them. A follow hint
    wins over an avoid hint at the same call.
  - The value is chosen in the type of the comparison and must survive the conversion to the call's type and back
    (`\result >= 255U` for an `unsigned char` gives 255; `== 300` for a `char` gives no hint).
  - A call of a pointer type gets a hint only from `\result == 0`, the null pointer, which is also the default: no
    constant names another address. A follow waypoint with `\result != \result`, which only NaN satisfies, gives NaN,
    the quiet NaN the search tries. Concrete witnesses give these values so ("Concrete witnesses").
  - Hints are computed at the call event, where constants are evaluated in the current frame.
- **Every confirmation comes from one complete run** in which all follow waypoints passed and no avoid waypoint passed,
  on the inputs of that run. The search only chooses the inputs of the next run, so a prediction that does not hold
  (the program takes another path, a term is not what the program computes) costs a restart, never soundness.
- **Defaults:** `ValidationOptions::defaultInput`, 0, converted to the requested type; pointers get null. 0 rather
  than 1, which NITWIT 3.x effectively uses: as the only default, 1 confirms one witness more on the 250-witness
  sample, but six fewer on `validation-crafted`.
- **The retry with defaults of 1** (`ValidationOptions::retryDefaultInput`, which the driver sets): a final run that
  ends without confirmation and leaves the search nothing it can solve was decided by the inputs between the
  waypoints, as in witnesses with the target only. The trigger is that rule, whatever ended the run, and the run gave
  some arithmetic input the default (else the retry would repeat it):
  - no waypoint failed: the monitor followed the witness to the end of the run (`main` returned, `exit`, `abort`, a
    failed `__VERIFIER_assume`, an endless loop without effect, undefined behaviour, an unsupported construct, the
    interpreter's memory or call-depth limit), the error function was called elsewhere than at the target, or a guard
    asked for a restart that the restart limit or a repeated run prevented ("Guards");
  - or a waypoint rejected the execution whose requirement has no term the search can change (no term, or only the
    opaque term), such as a branching on a value that depends on inputs only through control flow or through a
    library function. A waypoint whose terms the search tried in vain, or that reached the restart limit or a repeated
    run, gives no retry.
  - The retry is a second phase of the validation, and the last: the restart loop of the first phase again, with 1 for
    the inputs that are neither fixed nor hinted and the fixed inputs keeping their values. A waypoint without its
    outcome, or a guard that would end the run, searches and restarts as in the first phase ("Search"), with kept
    clauses, a restart limit of its own by the same formula, and repeated runs found among its own runs (with another
    default, the same fixed inputs make another run).
  - It confirms, or the verdict is that of the first phase: of its final run, or the verdict of the restart limit or
    the repeated run of a guard that ended it. NITWIT running out of its own memory in the retry does not end the
    validation, and the diagnostics of constraints first compiled in it are discarded. Its confirmations come from one
    complete run like any other.
  - **Budget:** since 1 keeps loops like `while (__VERIFIER_nondet_int())` going for ever, the runs of the retry
    together may take the events of all runs of the first phase and `retryExtraEvents` (2^22) more; the run that
    exceeds it ends the retry. A retry that does not confirm thus at most doubles the work of a validation, plus about
    0.3 s (2 s in a loop that requests inputs in every round). The constant leaves room for such a loop where the first
    phase was one short run, as for witnesses with the target only; the events of the first phase scale the budget
    with the program and the witness, which the runs of the retry follow as those of the first phase did. Events are
    the statements, conditions, calls and returns that the interpreter reports (`interp/observer.hpp`).
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
    `a op' b` that must hold is a relation of its two sides; one that must fail is the relation `!(a op' b)`, which
    inversion solves with the negated operator, but which NaN satisfies where that operator does not (`!(x < 1.0)`).
    An operand the run did not evaluate (`a && b` where `a` was false) is unknown: making `a` hold is all the search
    can do there. `c ? x : y` is solved through the operand the run evaluated, with `c` keeping its value, else by
    changing `c`: the other operand is unknown then, like a missing operand of `&&`. A goal that failed is not
    decomposed again for the same atom while the same guards hold (one that failed under a conjunct solved before may
    hold without it); this only saves cost on shared subterms (`c = c || c` in a loop), and a pass decomposes at most
    20,000 goals (`SearchLimits`).
    - **Truth values** are 0 or 1 whatever the inputs: comparisons, `!`, `&&`, `||`, conversions to `_Bool`, `_Bool`
      inputs (the search keeps them 0 or 1), the constants 0 and 1, and integer conversions and `&`, `|` and `^` of
      truth values. btor2c and xcsp combine conditions with `&` and `|`, like
      `assume(!((x == 0 & y == 0 & z == 1) | 0))`, and btor2c stores them in `unsigned char`: `a & b` and `a | b` of
      truth values are decomposed like `a && b` and `a || b`, the run having evaluated both operands, and an integer
      conversion of a truth value passes the atom on. Other operands, like the bit `(int)x & 1`, are left to inversion
      and candidates. Whether a term is a truth value is determined once per term and search.
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
    - **Special values** of the input's type: the maximum, the maximum − 1, the minimum and the minimum + 1 of an
      integer type, NaN, +∞, −∞ and −0.0 of a floating type. They satisfy relations that no other value does:
      overflow checks like `(x + 10) < x` at the maximum, where `+` wraps around as the run continues after a signed
      overflow; `x == x` false (`nan_double`) with NaN only; `!(x < x + 1e-6)` (neural-networks) with NaN or +∞;
      `1.0 / x < -1e300` with −0.0; and a `char` id of pals that must exceed the others, which kept clauses keep apart,
      with 127. But the program rarely goes on where the witness does with them: NaN satisfies `!(0 * x + y < 1)`
      (neural-networks' linear_1), where y = 1 is right, and an id of 127 leaves no greater id for a later comparison.
      So only a third pass tries them ("Values of earlier runs"), and there after the other values of all inputs of
      the relation, never in a repair. A clause that blocks one adds its constants as for any other value:
      `(r - 1) * (r - 1) < a`, blocking r = 2147483647 for `r * r - a > 0` with a = 3, gives r = 2 (fermat2).
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
    allowed, after all others; and only if that fails too, a third pass also tries the special values.
  - **Cost:** each value tried is evaluated against the relation and the clauses that mention its input, and a
    repair solves a clause for each value it repairs, so the kept clauses of an input and repairs multiply the work.
    A search therefore evaluates at most 300,000 terms, all passes and repairs included, well under a second; beyond
    that it ends without values, and the log says that it stopped (`SearchLimits`).
  - If every clause of the requirement then holds or is unknown, and at least one input has a new value, the new
    values become fixed inputs and the run restarts (`fixing input 3 (...) to 5 (inversion)` in the log); otherwise
    the waypoint fails (exit 241). The log (`-v`) says what the waypoint needed and what was chosen, e.g.
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
    number of distinct values fixed before. If the search proposes the fixed inputs of an earlier run of the phase
    ("Defaults") and no input was fixed to a new value since that run, the next run and its search would repeat it:
    the execution is rejected (exit 241; UNKNOWN, exit 4, where a guard asked for the restart, "Guards"), with "the
    search proposes the inputs of run n again". With a new value fixed since, the same inputs may lead elsewhere: the
    search after the repeated run knows more values to avoid. The oracle finds such a run by a fingerprint of the
    fixed values (the sum of their hashes) and the number of distinct values, and compares the values exactly where
    both agree.
  - The limit is `min(ValidationOptions::maxRestarts, 16 + 2 · waypoints)` (`restartLimit`) per phase, for the
    restarts of waypoints and guards together. A waypoint can need a restart for each prediction that fails, and a
    loop counter one per iteration; 16 cover witnesses with few waypoints. The cap of 10,000 bounds long witnesses,
    whose runs would exceed the time limit before.
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
  not do. An SMT solver could replace the candidates behind the same requirements (`docs/roadmap.md`). An avoid
  assumption evaluated in a hot loop records the strict terms of its constraint at each evaluation and can fill the
  term table, after which later values get the opaque term. A special value can be a size: `int array[n][n]` with
  n = 32767, the maximum of a `short` (array-patterns), takes 4 GiB, more than NITWIT's memory, so the run ends with
  exit 251 where the next waypoint would fail (exit 241).

### Guards

Programs state what they assume about their inputs by ending the run where it does not hold: `assume_abort_if_not(c)`,
`if (!c) abort();`, `__VERIFIER_assume(c)`. With values the search chose for one waypoint, a run often ends at such a
guard later. The monitor treats guards like waypoints whose requirement is that the run goes on (`guard.hpp`), in every
segment:

- **What a guard is,** decided from the AST:
  - an `if` of which one branch consists only of a call of `abort`, `exit`, `_Exit` or `__assert_fail`, inside blocks
    of one statement or after labels, and the other branch, if there is one, does not. A function of that name that
    the program defines does not count, since the interpreter calls the definition, which may return or call the
    error function (`void abort(void) { reach_error(); }`); neither does the error function. Its requirement is
    `t == 0` for the condition's term `t` if the `then` branch ends the run, `t != 0` if the `else` branch does
    (glibc's `assert`: `if (e) ; else __assert_fail(...);`);
  - a call of the builtin `__VERIFIER_assume` (not a definition of the program, which may contain a guard itself):
    `t != 0` for the argument's term `t`.

  `?:`, a branch with more than the call, and assumptions as endless loops (`L: goto L;`, ldv's `ldv_assume`) are no
  guards. A guard whose value has no term, or the opaque term, is ignored.
- **Passed:** a run that goes on past a guard keeps its requirement as a kept clause ("Search"), so that the search
  does not choose values for a later waypoint that end the run there.
- **Ending:** a run that takes the side that ends it asks the search for inputs that satisfy the requirement, as for
  a follow waypoint that fails. With new values, it restarts with them (``the guard at 6:5 (abort) would end the run``
  in the log); without, the run goes on to the end the guard gives it, as before. Guard restarts count against the
  restart limit; where the limit or a repeated run prevents one, the verdict is UNKNOWN (exit 4), and the retry may
  follow ("Defaults").
- **Witness waypoints win:** a guard at a branching where a waypoint of the current segment is evaluated is left to
  the witness, whose requirement is kept or searched for as usual. A follow waypoint into the `abort` of a guard
  (Goblint writes 304 such witnesses in SV-COMP 2026) is followed, and the run aborts as before, without a search
  against it, which the kept requirement of the waypoint would defeat anyway.
- In the retry ("Defaults"), guards work as in the first phase.

### Strict inputs

`ValidationOptions::strictInputs` (the driver's `--strict-inputs`) accepts only the inputs the witness gives, as a
validator would that stops at every value it would otherwise have to choose. It checks that a witness is concrete,
like the ones NITWIT writes ("Concrete witnesses").

- **Given inputs:** an input is given when a follow `function_return` waypoint of the current segment hints its value
  at its call ("Values"). Avoid waypoints give none, nor do defaults, the search and fixed values: there are no
  restarts and no retry. A relation like `\result > 5` gives a value (6) as a hint does, so a witness need not pin
  every value to be followed, but each input must have a follow waypoint.
- **An input the witness does not give** gets the default, so that the run reaches the next event, and there the
  monitor stops the run: the verdict is Unsupported (exit 4), "the witness does not give the value of input 1 (the call
  of __VERIFIER_nondet_int at 5:35, occurrence 0)". A read of uninitialized memory is such an input, since no waypoint
  can give it; so is one in a constraint. A run that ends before another event (an uninitialized read in `return x;`
  of `main`) gets the same verdict.
- **No search:** a follow waypoint that fails, or an avoid waypoint that passes, rejects the execution (exit 241) in
  the first run; a guard that would end the run ends it.

## Concrete witnesses

With `--write-witness <file>`, the driver writes the run that confirmed a witness as a witness of its own, in format
2.2: one segment per input of the run, in request order, each a follow `function_return` waypoint at the `)` of the
call that requested it with a constraint that gives exactly the run's value, then the target at the call of the error
function (`concrete.hpp`). Symbiotic writes its witnesses this way. Since every input is given, a validation with
strict inputs follows it in one run without choosing a value, and so does NITWIT without them.

- **Constraints:** in format 2.2, a `function_return` constraint has the format `ext_c_expression`: a C expression
  that may use `\result`, free of side effects and of undefined behaviour (sv-witnesses, `Witness-Format.md`,
  "Expression Evaluation"). 2.0 allowed only `\result op constant` in `acsl_expression`, which cannot give NaN. The
  written constraints:

  | Value | Constraint |
  |---|---|
  | integer, signed | `\result == -5`; the minimum of 64 bits as `\result == -9223372036854775807 - 1` |
  | integer, unsigned (also `_Bool`) | `\result == 4294967295U` |
  | null pointer | `\result == 0` |
  | finite floating value | `\result == 0x1.99999ap-4f`: a hexadecimal constant with the type's suffix (`f`, none, `L`), exact also for subnormal values; −0.0 is `-0x0p+0`, which +0.0 also satisfies, but the hint gives −0.0 |
  | +∞, −∞ | `\result > 0x1.fffffffffffffp+1023`, `\result < -0x1.fffffffffffffp+1023`: beyond the largest finite value of the type |
  | NaN | `\result != \result`, which only NaN satisfies; the hint gives the quiet NaN |

- **Locations:** line, column and function of the call's `)`, which must identify that evaluation point and no
  other (checked through the location map), and for the target the first character of the innermost statement or
  full expression that contains the call of the error function, as the format requires. The program's path as NITWIT
  got it is the file name of every waypoint and the input file of the metadata, with its SHA-256.
- **Metadata:** producer NITWIT with its version, the property `CHECK( init(main()), LTL(G ! call(f())) )` of the
  error function, the data model. The uuid (RFC 9562, version 8) is taken from a SHA-256 of the witness without
  uuid and creation time, so that a validation writes the same witness every time, except for the creation time.
- **The check:** the written text is read back with the witness parser and validated with the same options and strict
  inputs. Only a witness that this validation confirms is written; otherwise the reason is reported and nothing is
  written. The check repeats the confirming run once; `ConcreteTask::check` (the driver's `--no-witness-check`)
  leaves it out.
- **No witness** is written where a witness cannot give the run:
  - an input that is not the result of a call: a read of uninitialized memory, the bytes of
    `__VERIFIER_nondet_memory`;
  - a value that no constant gives: an integer beyond 64 bits, a non-null pointer (NITWIT never chooses one);
  - a call whose `)` is in a header or in text that the preprocessor changed, or whose location matches more than one
    call (two physical lines of the preprocessor's output that are one line of the program);
  - more inputs than `ConcreteTask::maxInputs` (1,000,000, which covers the longest witnesses of the SV-COMP 2026
    corpus; a waypoint takes about 200 bytes plus the program's path), or than the oracle records
    (`TermLimits::maxInputs`).
- **Undefined behaviour:** where the run continued after signed overflow or another undefined behaviour without an
  end (`interp/README.md`), the witness follows what GCC-compiled code does on x86, which the C standard leaves open.
  The driver reports the first such operation.
- **What a concrete witness proves:** that NITWIT's interpreter reaches the error function with these inputs. It is
  as right as NITWIT's semantics of C and of the library: a strict validation by NITWIT itself is no independent
  check. Other validators may read it differently where C leaves the order of evaluation open: two inputs in one
  full expression (`f(__VERIFIER_nondet_int(), __VERIFIER_nondet_int())`) appear in NITWIT's order, and a validator
  that evaluates them in another order meets the waypoints in another order.

## Known gaps

- **Values the search does not find** ("Limits of the search"). Of all SV-COMP 2026 unreach-call witnesses
  (`docs/evaluation.md`), 47 of buggy programs and 1,094 of correct ones reach the restart limit, mostly through guards
  that a loop evaluates once per input ("Guards"). The waypoints that fail need:
  - values that depend on inputs only through control flow, or through memory, pointers or library functions that
    terms do not follow: requirements without a term or on the opaque term (the `aws` `byte_buf` programs, the
    `v_FIFO` of btor2c);
  - a relation that no inversion and no candidate satisfies, or none together with the kept clauses: bits combined
    with `&` and `|` (btor2c's `(int)x & 1`), SAT problems over truth values (xcsp), which the greedy search does not
    backtrack in, polynomials (`bresenham`), `?:` over `signed char` (pals).
- **Paths between waypoints:** the search knows the requirements of waypoints and guards only. Where values change the
  path at another condition no waypoint covers, the run leaves the witness without a failing waypoint: a null pointer
  is read (exit 4), or the program ends before the target segment or in it without calling the error function (exit
  242, 250). Keeping the conditions of all branchings of a run, or a concolic search with an SMT solver
  (`docs/roadmap.md`), would cover them. The retry with defaults of 1 ("Defaults") covers some. A run that does not
  end, like `systemc/toy1` with inputs 0, gets no retry.
- **Guards** are only those of "Guards": an assumption that loops for ever or returns, or `__assert_fail` in `?:`, is
  not steered, and neither is a guard whose condition depends on inputs only through control flow or through the
  opaque term. Guard restarts share the restart limit with the waypoints, and each restart satisfies one evaluation of
  a guard: a loop that reads a new input and assumes something of it in every iteration (the `hardness` programs on
  which test-to-witness's witnesses run) takes a restart per iteration and can reach the limit, as can a program with
  many assumptions that the default values break one by one (xcsp).
- **Unsupported constructs** of the interpreter end a run with exit 4: threads (`pthread_create`), a longjmp to a
  setjmp that the interpreter cannot resume (`interp/README.md`, "Non-local jumps"), files, signals, `atexit`, the
  `scanf` family, inline assembly, arithmetic on complex values and the others listed in `interp/README.md`
  ("Library") and `sema/README.md`.
- Long runs can exhaust the host's memory before the interpreter's memory limit; the driver then reports exit code
  251. The tables that would grow with the length of a run are bounded. A run that requests inputs without end stays
  bounded in the validator: the oracle records the first `TermLimits::maxInputs` requests of a run (112 bytes each, at
  most 470 MB), and later ones have the opaque term (`TermTable::input`), which the search never changes. The search
  reads the recorded inputs it needs from the oracle (`SearchInputs`) instead of a copy of all of them per search.
  With the term table (at most about 400 MB), such a run stays below 900 MB. The memory model's table of dead heap
  blocks grows with the live blocks only (`interp/README.md`, "Memory: behaviour to keep"). Not bounded this way:
  - an endless loop that reads inputs and allocates grows the program's heap until NITWIT's own memory runs out,
    before the interpreter's limit of 4 GiB of live objects (three Goblint `forester-heap` witnesses: exit 251 at
    4.5 GB under an 8 GB address-space limit);
  - the memory of the witness grows with its size: test-to-witness's witnesses with 400,001 and 600,001 segments are
    confirmed at 1.4 and 2.0 GB.

## Tests

- `tests/unit/validate/`: each rule above, with small programs, witnesses built in code, and scripted runs
  (`validate_test_support.hpp`, whose `FakeExecution` records terms as the interpreter does);
  `end_to_end_test.cpp` with the interpreter and the terms it records (checked, also in constraints:
  `RunOptions::checkTerms`), so with the search: a constraint on a computed value, a loop counter, the input check of
  RERS, the result of a defined function, avoided switch cases and narrowing conversions, a NaN input, truth values in
  `unsigned char` combined with `&`, guards (`assume_abort_if_not` searched for, a follow and an avoid waypoint at
  its `if`, `__VERIFIER_assume`, `exit` and `_Exit`, the program's own `abort`), the retry (searching, its budget,
  after a waypoint on the opaque term) and longjmp (a witness followed across one, the search for its value, a function
  it leaves, a local changed before it in sv-benchmarks' `poison-reduced_true`); `guard_test.cpp` which `if`
  statements are guards;
  `search_test.cpp` the search on term tables built by hand, one test per rule (inversion through every invertible
  operation, candidates and their order, the special values of each format and when they are tried, the negation of a
  floating comparison, kept clauses and their index, repairs, unknown atoms, `&&`, `||` and `?:`, `&` and `|` of truth
  values (nested, through conversions, xcsp's clauses, bits left to candidates), shared subterms, switches, avoid
  waypoints, the order of inputs and values, the three passes, the memo under guards, the budgets of goals and
  evaluations, the explanation)
  and the examples above (the input check of RERS, a loop counter, `if (tmp)`); monitor tests of the search over
  several runs: an input that two waypoints would change back and forth, a run that would repeat an earlier one and one
  that repeats its inputs with a value tried since, case ranges, avoided cases of a switch, a repaired clause, the limit
  of kept clauses; monitor tests of guards (searched for, kept, not passable, a witness waypoint at one, at the restart
  limit, repeating a run, `__VERIFIER_assume`); restart tests of the retry (its search for follow and avoid waypoints,
  its own restart limit, its budget over the runs of both phases, its trigger after a waypoint without term but not
  after one the search tried) and an oracle test of repeated runs with another default. `concrete_test.cpp`: concrete
  witnesses with the interpreter (every input at its call in request order, the metadata and the program's hash, the
  same witness for the same validation, special floating values, the target at the innermost full expression of the
  error call, the cases without witness including a location of two calls, undefined behaviour, the constraint of every
  kind of value), strict inputs (an input without waypoint, no search, avoid waypoints, a guard that ends the run, no
  retry, a read just before the end), NaN given and hinted, the check left out, and the record of the confirming run;
  oracle tests of required hints and of taking the inputs, value tests of null pointers and NaN, location tests of the
  positions a witness gives and of the statement or full expression around a call; an end-to-end test of the
  locations a target may have.
- `tests/corpus/validate/`, label `corpus`: runs `nitwit` per witness, as a process with a timeout of 30 s, and checks
  - `testfiles/yaml-witnesses` and the 2.x witnesses in `testfiles/sv-comp-2019`: every witness confirmed, except
    those named `*.invalid`, which must not be;
  - `validation-crafted` (expected results in `*.witness-validation.yml`): no invalid witness confirmed, and at least
    as many witnesses right as 3.x;
  - the 250-witness sample (`samples/unreach-call-25-per-producer.tsv`): no witness of a correct program confirmed,
    and at least as many witnesses of buggy programs (without Paulina's crafted ones) confirmed as 3.x; every
    difference to the 3.x results in `results/2026-10-20-nitwit-strict-v3-*` is printed.
