# NITWIT 3.x: game plan for SV-COMP 2027

NITWIT 3.x is the version that competes in SV-COMP 2027 (track "Validation of Violation Witnesses v2"). The C++23
re-implementation (NITWIT 4, branch `next`, see `next/docs/plan.md`) will not be ready in time. So 3.x gets the fixes
that are cheap, measurable and low-risk in the PicoC code base. Everything else moves to NITWIT 4, where it is part of the
design instead of a patch.

Measurements and the full list of findings: `E:\workspace\playground\svcomp-2026-witnesses\PLAN.md` (P1 to P4).

## Rules for every 3.x change

- **No wrong confirmations.** Witnesses for correct programs and the invalid witnesses in `validation-crafted/` must stay
  rejected: 0 of 34 and 0 of 59 today. A confirmation must always come from a concrete execution. Choosing a value for a
  nondeterministic call is allowed; flipping the outcome of a branch is not.
- **Regression check before every commit:**
  1. `run-tests.sh` for `sv-comp-2019` (only the 4 known failures) and `yaml-witnesses` (12/12).
  2. GraphML results on the 84 test witnesses, unchanged in 32 and 64 bit.
  3. `validation-crafted`: run `scripts/run_vc.sh`.
  4. The 250-witness sample: run `scripts/run_sample.sh`, then `analyze.py`.
- **Before the release:** a larger sample of 100 witnesses per producer, with the SV-COMP timeout of 90 s.

## 3.1.0: release what is on `master`

| # | Item | Effort | Status |
|---|---|---|---|
| R1 | Release P1 and P2 (`f7c173c`): version 3.1.0 in `nitwit.sh`, `build.sh`, then a new Zenodo version and the DOI in fm-tools | small | **needs your go** |
| R2 | BenchExec: push `ffd329d` (exit 252 → UNKNOWN) to PR #1339 | trivial | **needs your go** |
| R3 | Push `master` and the tag `3.0.0` (on `6758ef2`) to GitHub | trivial | **needs your go** |

## 3.1.x: correctness fix first

| # | Item | Where | Effect | Effort |
|---|---|---|---|---|
| F0 | **Avoid waypoints on unresolved nondet values.** 3.x evaluates avoid assumptions with `ResolveNonDetInAssumptions = FALSE` and treats an unresolved value as "not passing". If the execution later uses the default value, it really passes the avoid waypoint, and an invalid witness can be confirmed (found by the NITWIT 4 review: `x = nondet(); if (x == 0) reach_error();` with `avoid x == 0` is confirmed). Fix: resolve the value to one that violates the avoid constraint (as already done for `function_return`), or reject. Measure on `validation-crafted` | `passesAvoidWaypoint` in `automaton_run.cpp` | removes a source of wrong confirmations | small |

## 3.2.0: value inference and robustness

These items are ordered by benefit per effort.

| # | Item | Where | Expected effect | Effort |
|---|---|---|---|---|
| F1 | **Nondet call as a condition**, e.g. `if (__VERIFIER_nondet_int())` or `while (__VERIFIER_nondet_bool())` | `ConditionCallback`, `LastNonDetValue` | Ultimate witnesses (Automizer, Taipan, Kojak), part of the 25 crafted misses | small |
| F2 | **Relational conditions on unresolved nondet variables** (`x == c`, `x < c`, ...) | condition check point, `CheckAndResolveVariable` | same as F1, plus loops with a nondet bound | medium |
| F3 | **Non-equality assumptions** resolve values: `x > 5` → 6, `x != 0` → 1 | `CheckAndResolveVariable` | 238 CPAchecker witnesses contain them | small |
| F4 | **Bigger stack:** run the interpreter in a `pthread` with 1 GiB of stack | `main.cpp` | no more crashes on deeply recursive programs (`fuzzle-programs`) | small |
| F5 | **`exit()` reports 242** instead of 4 | `PlatformExit` path in `main.cpp` | cosmetic (both are UNKNOWN) | trivial |
| F6 | **`long double`** treated as `double` | `lex.cpp`, `type.cpp` | part of the 30 `';' expected` runs | small |
| F7 | **Anonymous struct/union members:** give each member a unique name and resolve member lookups through it | `type.cpp` | 5 runs (glibc `pthread_cond_t`) | medium |
| F8 | **`switch` `default`:** avoid and follow waypoints check that the value differs from every `case` label | `ParseStatement` (`switch`) | crafted `switch.*` | medium |

F1 to F3 are restricted versions of the general value search in NITWIT 4: they change a value only when it came
**directly** from a nondeterministic call (F1) or is still unresolved (F2, F3). F1 has one subtle point. The value must
not be stored anywhere, as in `if ((x = __VERIFIER_nondet_int()))`; otherwise changing it would not correspond to a
different return value.

## Moved to NITWIT 4

| Item | Why not in 3.x |
|---|---|
| General search: restart with other nondet values when a follow waypoint fails | needs deterministic replay of the whole interpreter state; PicoC keeps state in globals |
| Symbolic tracking with an SMT solver | needs value provenance through every operation |
| Function typedefs, typedef casts in `const` global initialisers, enum forward declarations | the PicoC parser evaluates while parsing; these need a real declaration/type layer |
| GNU statement expressions with `typeof`, `__builtin_va_list`, `_setjmp`, `__asm__` labels | same |
| Member access through a pointer that NITWIT reads as `long` | type confusion inside PicoC's value model |
| Column of `?` in ternaries, `do`/`while` location, multi-line calls | need tokens with full source ranges, which NITWIT 4 has from the start |
| Target-only witnesses | need a search for a path to the error |
| Properties `no-overflow`, `valid-memsafety`, `termination` | need a precise memory model and undefined-behaviour checks |
| Waypoint action `cycle` | needs termination witnesses (property `termination`) |

## Decisions still open from earlier

- **Affiliation and categories in fm-tools:** RWTH Aachen as the institution; `C.ReachSafety`, `C.SoftwareSystems` and
  `C.ValidationCrafted` as categories, or `C.Overall`.
- **Commit e-mail address:** so far `jana@moduleworks.com` from the git configuration.
