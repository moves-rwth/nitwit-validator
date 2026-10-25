# NITWIT 4: roadmap

The open work, roughly in order. What NITWIT does is described in `README.md` and the module READMEs, how it is
designed in `docs/architecture.md`.

## Witness format 1.0 (GraphML)

The parser exists (`src/witness/`, `parseGraphmlWitness`), but the driver gives every GraphML witness UNKNOWN (exit 4).
Missing is a monitor for the automaton in `validate`, next to the segment monitor of format 2.x
(`docs/architecture.md`, "Validation"), with the input oracle, the search and the verdicts of format 2.x. Done when
the GraphML witnesses in `testfiles/` give at least the results of NITWIT 3.x.

## Where NITWIT does not confirm

NITWIT does not confirm 5,602 of the 14,841 SV-COMP 2026 witnesses of buggy programs; `docs/evaluation.md` ("Why
NITWIT does not confirm the other 5,602") has the reasons. The remedies, ranked by expected gain per effort. The gain
counts the witnesses of the addressed group that a validator outside the producer's family confirms, so it is an upper
bound.

| Rank | Remedy | Gain | Effort |
|---|---|---|---|
| 1 | Search after the retry with inputs 1 (implemented after 4.0.0) | up to 78 | small |
| 2 | Aborts and exits in `assume` wrappers as failed requirements, searched and kept | up to 85 | small–medium |
| 3 | `setjmp`/`longjmp` in the interpreter | 66 of 91 | medium |
| 4 | NaN, ±infinity and extreme values as candidates | 12–30 | small |
| 5 | `&` and `\|` of truth values decomposed in the search | up to 44 | small–medium |
| 6 | More candidates or backtracking where kept clauses exclude them (several inputs, `?:`) | up to 104 | medium |
| 7 | Terms for nondet memory, pointer and allocation-size inputs, type punning, `fmin`/`fmax` | up to 76 | medium–large |
| 8 | A bit-precise solver with floating point behind the requirements | 166; about 315 with ranks 5–6 | large |
| 9 | A path search between waypoints (flip the last input-dependent condition before an early end) | about 210 | large |
| 10 | A progress budget for runs that pass no new segment, handing them to ranks 1 and 9 | up to 100 | medium |
| 11 | Threads | unknown | large |

1. The retry with inputs 1 passes more segments on 520 witnesses, mostly Goblint's, but in 4.0.0 a waypoint that fails
   in it ends it without a search. The retry now searches (`src/validate/README.md`, "Defaults"); its gain is not
   measured yet.
2. A program that aborts or exits in `assume_abort_if_not`, `assume` or `assume_exit_if_not` ends the run (exit 4 or
   242) instead of telling the search which condition the inputs must meet.
3. `_setjmp` ends 91 runs as unsupported; validators outside the producer's family confirm 66 of the 70 of them with
   SV-COMP results.
4. Floating-point relations and overflow checks such as `(x + 10) < x` need values the candidates do not contain.
5. btor2c and xcsp programs combine truth values with `&` and `|` (283 witnesses), which the search does not
   decompose.
6. The greedy search does not backtrack across clauses, so kept clauses exclude the candidates of one input (261
   witnesses) or of several inputs and `?:` (350).
7. Values computed through memory that `__VERIFIER_nondet_memory` fills, through pointers or allocation sizes, type
   punning or `fmin`/`fmax` get the opaque term (536 witnesses), which the search cannot steer.
8. A bit-precise solver with floating point, an SMT solver for instance, would replace the candidates behind the same
   requirements and solve nonlinear relations: `*`, `/`, `%`, shifts, an input twice (410 witnesses); with ranks 5 and
   6 it covers most failed searches. A concolic search on top of it would also cover rank 9.
9. Where values change the path at a condition no waypoint covers, the run ends early without a failing waypoint
   (no term: 730 witnesses; early ends: exit 242, 250 and aborts). Flipping the last input-dependent condition before
   the early end would explore those paths.
10. 416 runs time out in loops that pass no new segment; a budget would end them early enough for the retry and the
    path search.
11. 275 witnesses use threads (CPAchecker 266); SV-COMP's validators did not run on them, so the gain is unknown.

**No gain:** Goblint's CIL locations and `abort` branches (981 witnesses: columns inside statements and `case`
labels, assumptions around calls that CIL splits, branches into `abort()`), Ultimate's two branchings per evaluation
of a loop condition (42), real undefined behaviour (28, verifythis) and test-to-witness's rounded floats in
`function_return` constraints (43). Validators outside the producer's family confirm 7 of the 981, none of the 42 and
43, and 22 of the 28.

**Not recommended:** reading a follow waypoint as passing at a later evaluation, as UAutomizer and Witch do, would
confirm up to 100 more witnesses, but `validation-crafted` requires the first evaluation.

## Exit code 252

NITWIT reports the error function called before the target as 241, because BenchExec 3.35 maps 252 to ERROR
(`docs/exit-codes.md`). Once BenchExec maps 252 to UNKNOWN, NITWIT exits with 252, as 3.x does.

## Later

- **An own preprocessor** that records expansion locations, so that columns in lines with macro invocations are exact
  and NITWIT needs no system `cpp`. Few programs need one (`docs/architecture.md`, "Tokens and preprocessing").
- **Other properties:** `no-overflow` and `valid-memsafety`, on the undefined-behaviour reports of the interpreter
  (signed overflow, invalid shifts, memory errors), and termination with the `cycle` waypoints of format 2.x.
- **The 32-bit libm:** NITWIT computes the math functions of i386 programs with the host's 64-bit libm, which differs
  in the last bit for some functions (`src/interp/README.md`, "Library"). A helper process with the 32-bit libm would
  remove the difference.
