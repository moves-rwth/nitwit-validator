# NITWIT 4: roadmap

The open work, roughly in order. What NITWIT does is described in `README.md` and the module READMEs, how it is
designed in `docs/architecture.md`.

## Witness format 1.0 (GraphML)

The parser exists (`src/witness/`, `parseGraphmlWitness`), but the driver gives every GraphML witness UNKNOWN (exit 4).
Missing is a monitor for the automaton in `validate`, next to the segment monitor of format 2.x
(`docs/architecture.md`, "Validation"), with the input oracle, the search and the verdicts of format 2.x. Done when
the GraphML witnesses in `testfiles/` give at least the results of NITWIT 3.x.

## Where NITWIT does not confirm

NITWIT does not confirm 5,402 of the 14,841 SV-COMP 2026 witnesses of buggy programs; `docs/evaluation.md`
("Why NITWIT does not confirm the other 5,402") has the reasons. The remedies that are still open, ranked by expected
gain per effort. The gain counts the witnesses of the addressed group that a validator outside the producer's family
confirms, so it is an upper bound.

| Rank | Remedy | Gain | Effort |
|---|---|---|---|
| 1 | `setjmp`/`longjmp` in the interpreter | 66 of 91 | medium |
| 2 | More candidates or backtracking where kept clauses exclude them (one input, several inputs, `?:`) | up to 155 | medium |
| 3 | Terms for nondet memory, pointer and allocation-size inputs, type punning, `fmin`/`fmax` | up to 53 | medium–large |
| 4 | A bit-precise solver with floating point behind the requirements | 153, more with rank 2 | large |
| 5 | A path search between waypoints (flip the last input-dependent condition before an early end) | about 350 | large |
| 6 | A progress budget for runs that pass no new segment, handing them to the retry and rank 5 | up to 101 | medium |
| 7 | Threads | unknown | large |

1. `_setjmp` ends 91 runs as unsupported; validators outside the producer's family confirm 66 of the 70 of them with
   SV-COMP results.
2. The greedy search does not backtrack across clauses, so kept clauses exclude the candidates of one input (234
   witnesses, many of them contradictory under NITWIT's reading of the witness) or of several inputs, `?:` and
   floating point (285).
3. Values computed through memory that `__VERIFIER_nondet_memory` fills, through pointers or allocation sizes, type
   punning or `fmin`/`fmax` get the opaque term (493 witnesses), which the search cannot steer.
4. A bit-precise solver with floating point, an SMT solver for instance, would replace the candidates behind the same
   requirements and solve nonlinear relations: `*`, `/`, `%`, shifts, an input twice (374 witnesses), and bits
   combined with `&` and `|` (285); with rank 2 it covers most failed searches. A concolic search on top of it would
   also cover rank 5.
5. Where values change the path at a condition no waypoint or guard covers, the run ends early without a failing
   waypoint (no term: 742 witnesses; early ends without a failing waypoint: about 1,600). Flipping the last
   input-dependent condition before the early end would explore those paths.
6. 416 runs time out in loops that pass no new segment; a budget would end them early enough for the retry and the
   path search.
7. 275 witnesses use threads (CPAchecker 266); SV-COMP's validators did not run on them, so the gain is unknown.

**No gain:** Goblint's CIL locations and `abort` branches (976 witnesses: columns inside statements and `case`
labels, assumptions around calls that CIL splits, branches into `abort()`), Ultimate's two branchings per evaluation
of a loop condition (42), real undefined behaviour (29, verifythis) and test-to-witness's rounded floats in
`function_return` constraints (43). Validators outside the producer's family confirm 6 of the 976, none of the 42 and
43, and 22 of the 29.

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
