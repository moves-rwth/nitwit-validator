# NITWIT 4: roadmap

The open work, roughly in order. What NITWIT does is described in `README.md` and the module READMEs, how it is
designed in `docs/architecture.md`.

## Witness format 1.0 (GraphML)

The parser exists (`src/witness/`, `parseGraphmlWitness`), but the driver gives every GraphML witness UNKNOWN (exit 4).
Missing is a monitor for the automaton in `validate`, next to the segment monitor of format 2.x
(`docs/architecture.md`, "Validation"), with the input oracle, the search and the verdicts of format 2.x. Done when
the GraphML witnesses in `testfiles/` give at least the results of NITWIT 3.x.

## Where NITWIT does not confirm

> **CENSUS placeholder:** this section will list the causes of the unconfirmed witnesses of buggy programs, with
> their numbers and the remedies, from the census of the evaluation's unconfirmed witnesses. Until then, see
> `src/validate/README.md` ("Limits of the search", "Known gaps") and `docs/evaluation.md`.

## Exit code 252

NITWIT reports the error function called before the target as 241, because BenchExec 3.35 maps 252 to ERROR
(`docs/exit-codes.md`). Once BenchExec maps 252 to UNKNOWN, NITWIT exits with 252, as 3.x does.

## Later

- **An own preprocessor** that records expansion locations, so that columns in lines with macro invocations are exact
  and NITWIT needs no system `cpp`. Few programs need one (`docs/architecture.md`, "Tokens and preprocessing").
- **Other properties:** `no-overflow` and `valid-memsafety`, on the undefined-behaviour reports of the interpreter
  (signed overflow, invalid shifts, memory errors), and termination with the `cycle` waypoints of format 2.x.
- **An SMT solver behind the search:** a concolic search that replaces the candidates of the search behind the same
  requirements, and solves relations with an input on both sides and conditions that no waypoint covers
  (`src/validate/README.md`, "Search").
- **Threads** (`pthread_create`), and `setjmp`/`longjmp`.
- **The 32-bit libm:** NITWIT computes the math functions of i386 programs with the host's 64-bit libm, which differs
  in the last bit for some functions (`src/interp/README.md`, "Library"). A helper process with the 32-bit libm would
  remove the difference.
