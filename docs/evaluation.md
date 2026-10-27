# NITWIT 4: evaluation

NITWIT 4 at 0914f93, the release archive built by `tools/package.sh`, as an SV-COMP violation-witness validator on
the unreach-call witnesses of SV-COMP 2026, at SV-COMP's 90 s (2026-10-25). The 373 witnesses whose programs name
`setjmp` or `longjmp` ran again with the archive built from b146595, which supports them (2026-10-27, "setjmp and
longjmp"); the numbers below include those runs.

- **All witnesses:** of the 27,767 unreach-call witnesses in format 2.x whose program is in the dataset, NITWIT confirms
  9,526 of the 14,841 witnesses of buggy programs (64 %; 80 % without Goblint's) and none of the 12,792 witnesses of
  correct programs. `validation-crafted`: 96 of 100 right, no invalid witness confirmed. `testfiles`: all 45
  format 2.x witnesses right.
- **Against NITWIT 3.x,** on 1000 of these witnesses (100 per producer): NITWIT 4 confirms 601 of the 851 witnesses of
  buggy programs, 3.x 312; neither confirms a wrong one. On the 903 witnesses both decide, NITWIT 4 takes 15 s of CPU
  time, 3.x 348 s.
- **Against the other SV-COMP 2026 validators,** on the 11,738 witnesses of buggy programs they validated: NITWIT
  confirms 7,054, more than any of them alone (Witch 5,392), the five together 7,934. Validators outside the
  producer's family confirm 904 that NITWIT does not; NITWIT confirms 514 that no other validator does. NITWIT
  confirms no witness of a correct program, CPAchecker 4 and MetaVal 3.
- **Memory:** no run on the 1000 witnesses takes more than 1 GB (at most 842 MB). Of all witnesses, 5 runs do, up to
  4.5 GB ("Runtime" of "All witnesses").

## Method

Common to both runs:

- **Binaries:**
  - NITWIT 4: the archive built by `tools/package.sh` from 0914f93 (GCC 14, preset `release`, libstdc++ and libgcc
    static), unpacked; `nitwit/nitwit.sh`.
  - NITWIT 3.x: the last commit of the branch `old`, 8e093bf, exported with `git archive`, built with its `build.sh`
    (Release, GCC 13.3; `make -j4` instead of `-j8`; its last step, the submission archive, fails for lack of
    `dos2unix` after `bin/` is complete); its `nitwit.sh`, `bin/nitwit32` (32-bit) and `bin/nitwit64`.
- **Witnesses and verdicts:** the SV-COMP 2026 witness dataset (`docs/development.md`, "Corpus"). The expected verdict
  is that of the task's unreach-call property in sv-benchmarks (tag `svcomp26`): `false` (buggy program), `true`
  (correct program) or none (ldv tasks without an unreach-call verdict). Paulina's witnesses are the crafted witnesses
  of `c/validation-crafted`; `scripts/crafted_map.py` matches each to one with the same violation sequence: 41 valid
  (must be confirmed), 59 invalid (must be rejected), all on buggy programs.
- **Command line:** BenchExec's order, `nitwit.sh -p <prp> -32|-64 -w <witness> <program>`, from the directory of
  `nitwit.sh` (3.x's calls `./bin/nitwit$bits`). `<prp>` is sv-benchmarks' `c/properties/unreach-call.prp` (tag
  `svcomp26`), byte for byte the archive's `smoketest/unreach-call.prp`.
- **Program names:** the program runs under its original file name, a symbolic link `programs/<witness>/<name>`,
  `<name>` being the last path component of `programfile` in the witness's metadata, because NITWIT 4 rejects
  waypoints whose `location.file_name` names another file.
- **Limits:** `timeout --kill-after=5 90` and `ulimit -v 8000000` (8 GB of address space) per run, 5 runs in parallel.
  Under WSL2, `timeout` ends busy runs late, after up to 99 s of CPU time.
- **Measurement:** `/usr/bin/time -f "%e %U %S %M"` around `timeout`: wall time, user + system CPU time and maximum
  RSS, children included. The inputs were copied to WSL's own file system first (`scripts/mirror_sample.sh`).
- **Machine:** WSL2 Ubuntu 24.04, AMD Ryzen 9 7900X (12 cores, 24 threads), 30 GB.

The comparison with 3.x:

- **Sample:** `samples/unreach-call-100-per-producer.tsv`, drawn by
  `scripts/sample.py <dataset> unreach-call-100-per-producer 100`: 100 unreach-call violation witnesses in format 2.x
  per producer (seed 2027), 1000 in all, on 789 distinct programs; 738 run with `-32`, 262 with `-64`; format 2.0: 710,
  2.1: 290 (symbiotic 90, test-to-witness and witch all). Expected verdicts: 851 `false`, 147 `true`, 2 none.
- **Runs:** NITWIT 4's rows are those of the run over all witnesses. 3.x's results are those of 2026-10-22
  (`results/2026-10-22-nitwit-3.x-unreach-call-100-per-producer` in the dataset). 3.x's `nitwit.sh` does not `exec`
  its executable: on a timeout it dies before reaping `bin/nitwit32`, whose CPU time and memory are then missing; its
  3 timeouts were measured again with `bin/nitwit32` called directly, same limits (`remeasured.tsv`: 95.2 to 95.3 s
  CPU, up to 1.6 GB). 78 of the witnesses are also in the 250-witness sample of the corpus test, whose 3.x results come
  from runs under hash names: 3.x gives the same exit code on all 78.
- **Tables:** `scripts/compare_runs.py`.

All witnesses:

- **Witnesses:** `samples/unreach-call-all.tsv`, drawn by `scripts/sample.py <dataset> unreach-call-all all
  <sv-benchmarks checkout>`: the 27,767 unreach-call witnesses in format 2.x whose program is in the dataset (3,601
  others are not run); 14,841 on buggy programs, 12,792 on correct ones, 34 without verdict, and Paulina's 100. Plus
  `validation-crafted` (100) and `testfiles` (45 in format 2.x, 81 in format 1.0).
- **Runs:** new runs started only while the host had at least 8 GB of free memory. Every unconfirmed witness that
  must be confirmed ran again with `-v`; the reasons below come from the result lines and these logs. Results, logs
  and the classification (`census.tsv`) are in `results/2026-10-25-nitwit-4-0914f93-unreach-call-all` in the
  dataset, the runner and the classification scripts in its `scripts/census/`.
- **Other validators:** the verdicts of SV-COMP 2026 (tables `validation-of-violation-witnesses-v2`) of CPAchecker,
  MetaVal, Theta, UAutomizer and Witch; Theta gave no verdict on these tasks, and the witnesses of test-to-witness and
  witch were not validated. A validator is *outside the producer's family* unless it is UAutomizer for the Ultimate
  tools (Automizer, Taipan, Kojak), CPAchecker for CPAchecker, or Witch for symbiotic.

## Against NITWIT 3.x

Witnesses of buggy programs confirmed (exit 0), and the changes per witness:

| Producer | buggy | 3.x | NITWIT 4 | lost | gained |
|---|---|---|---|---|---|
| Automizer | 99 | 9 | 74 | 0 | 65 |
| CPAchecker | 100 | 45 | 77 | 1 | 33 |
| Goblint | 27 | 2 | 3 | 0 | 1 |
| Kojak | 99 | 19 | 66 | 0 | 47 |
| Mopsa | 100 | 50 | 76 | 1 | 27 |
| Paulina (41 valid, 59 invalid) | 100 | 18 | 37 | 2 | 21 |
| Taipan | 99 | 14 | 66 | 0 | 52 |
| symbiotic | 100 | 75 | 88 | 0 | 13 |
| test-to-witness | 27 | 20 | 27 | 0 | 7 |
| witch | 100 | 60 | 87 | 0 | 27 |
| **total** | 851 | **312** | **601** | 4 | 293 |
| **without Paulina** | 751 | **294** | **564** | 2 | 272 |

Four of the gained witnesses are on `longjmp` programs, which 3.x ends with exit 244 ("setjmp and longjmp" below).

- **Correct programs** (Goblint 72, test-to-witness 73, Kojak 1, Taipan 1): 0 confirmed by either tool. 3.x crashes
  on 5 runs (heap corruption, segmentation fault; BenchExec: ERROR), NITWIT 4 on none.
- **Paulina:** 3.x confirms 18 of the 41 valid crafted witnesses, NITWIT 4 37; neither confirms an invalid one. The 4
  valid ones NITWIT 4 misses: `while.3A1`, `while.3B1` (lost, below), `switch.4A1`, `switch.4B1` (exit 250; 3.x: 242).
- **Unknown expected verdict** (2): neither tool confirms.

### Lost confirmations

The 4 witnesses that 3.x confirms and NITWIT 4 does not are on buggy programs, and the two crafted ones are valid
witnesses, so each is a confirmation lost, not a soundness problem.

- **Paulina `while.3A1`, `while.3B1` (`796583053ff3`, `eba394825e26`).** The assumption `a == 10` at `count++;`
  (line 25) needs `n = 90` in `if (n > 0) a = a - n;`, which no waypoint covers. With n = 0 the assignment does not
  run, so `a` depends on `n` only through control flow and its term is the hinted input (100): "needs `in0 == 10`: no
  input values found" (exit 241; known gap "values that depend on inputs only through control flow").
- **CPAchecker `minepump_spec3_productSimulator` (`2fb20af996b7`).** CPAchecker writes one segment per call of
  `select_one`, each an assumption at the same location (the `{` at 952:3) with another value: `choice == 0`, then
  `choice == 2`. NITWIT's cascading rule (`src/validate/README.md`, "Cascading") evaluates the next segment's
  assumption at the same execution of the statement, where `choice == 2` cannot hold (exit 241). The witness means the
  next call; 3.x reads it that way.
- **Mopsa `systemc/toy1` (`aadb77432e42`):** a witness with the target only. The run with inputs 0 does not end
  (timeout), so there is no retry with inputs 1, with which 3.x reaches the error at once.

### Runtime

The runtime is compared on the witnesses both tools decide: the runs that end without a timeout or a limit of either
tool. A timeout gives no answer either way, and NITWIT ends a run early only where the witness can no longer be
followed (`docs/exit-codes.md`).

| | witnesses | 3.x | NITWIT 4 |
|---|---|---|---|
| both decide (no timeout, no limit, 3.x not at its transition limit) | 903 | 348 s | 15 s |
| - of them both at most 0.05 s (front end and start-up dominate) | 804 | 4.0 s | 1.6 s |
| 3.x stops at its limit of 5 M transitions, NITWIT 4 ends (NITWIT 4 confirms 56) | 70 | 192 s | 111 s |
| all 1000 (timeouts: 3.x 3 with 286 s, NITWIT 4 12 with 1,122 s) | 1000 | 832 s | 1,262 s |

- **Per witness:** of the 973 witnesses both runs finish, NITWIT 4 is faster on 200, equally fast on 727 (most runs
  take less than the 10 ms resolution of the CPU time) and slower on 46. It is slower by more than 1 s on 3: the two
  confirmations where 3.x stops at its transition limit after 0.6 to 0.7 s (exit 4), Mopsa's
  `loop-acceleration/simple_2-2` (61 s) and `loops-crafted-1/nested3-2` (48 s), and a `hardness` program of
  test-to-witness (1.0 s), whose guards restart the run once per input of a loop.
- **Timeouts and out of memory:** NITWIT 4's 12 timeouts are ldv, systemc, product-lines, combinations and btor2c
  programs and Mopsa's `loop-acceleration/nested_1-2`; on 11 of them 3.x stops within 3.3 s (exit 4), and it confirms
  `systemc/toy1`. Its 12 runs out of memory are aws harnesses that allocate more than the interpreter's limit.
- **Memory:** max RSS median 5.8 MB (3.x 5.2 MB), 90th percentile 12 MB (8 MB), maximum 842 MB (1.6 GB); no run of
  NITWIT 4 over 1 GB (3.x: 2).

## All witnesses

### Confirmations

Witnesses of buggy programs, without Paulina's (37 of whose 41 valid witnesses are confirmed). "SV-COMP results": the
witnesses the other validators ran on; "others": confirmed by a validator outside the producer's family; "others
only": confirmed by such a validator and not by NITWIT.

| Producer | buggy | confirmed | SV-COMP results | others | others only |
|---|---|---|---|---|---|
| Automizer | 2,665 | 2,024 (76 %) | 1,442 | 647 | 107 |
| CPAchecker | 3,619 | 2,774 (77 %) | 3,353 | 2,934 | 205 |
| Goblint | 3,342 | 306 (9 %) | 3,342 | 637 | 352 |
| Kojak | 665 | 472 (71 %) | 665 | 368 | 52 |
| Mopsa | 208 | 147 (71 %) | 192 | 167 | 33 |
| Taipan | 885 | 592 (67 %) | 885 | 475 | 79 |
| symbiotic | 1,859 | 1,751 (94 %) | 1,859 | 1,555 | 76 |
| test-to-witness | 1,228 | 1,127 (92 %) | – | – | – |
| witch | 370 | 333 (90 %) | – | – | – |
| **total** | **14,841** | **9,526 (64 %)** | 11,738 | 6,783 | 904 |

### Soundness

No wrong confirmation: 0 of the 12,792 witnesses of correct programs, 0 of the 118 invalid crafted witnesses
(`validation-crafted` and Paulina's copies), 0 of the 15 invalid test witnesses (8 in format 2.x; the 7 in format 1.0
give exit 4), and none of the 34 witnesses without verdict. On correct programs, most runs end because a follow
branching fails (5,875), the program aborts (1,964), the witness does not fit the program (1,871) or the search reaches
its restart limit (1,094).

### Against the other validators

The 21,647 witnesses with SV-COMP 2026 results (`validation-of-violation-witnesses-v2.C.unreach-call.*`) and a NITWIT
run: 11,738 on buggy programs, 9,909 on correct ones.

| Validator | buggy confirmed | % | wrong (correct program confirmed) |
|---|---|---|---|
| NITWIT 4 | 7,054 | 60.1 % | 0 |
| Witch | 5,392 | 45.9 % | 0 |
| CPAchecker | 4,906 | 41.8 % | 4 |
| UAutomizer | 4,452 | 37.9 % | 0 |
| MetaVal | 2,858 | 24.3 % | 3 |
| Theta | 0 | 0 % | 0 |
| any of the five | 7,934 | 67.6 % | 5 |

NITWIT confirms more than any single validator, the five together confirm 880 more, and NITWIT makes no wrong
confirmation where CPAchecker and MetaVal make 4 and 3. Counting only validators outside the producer's family, the
five confirm 6,783. 1,394 witnesses are confirmed by another validator and not by NITWIT, 904 of them outside the
producer's family; 514 are confirmed by NITWIT only.

### Why NITWIT does not confirm the other 5,315

"Others": confirmed by a validator outside the producer's family.

| Reason | Exit | Witnesses | Producers | Others |
|---|---|---|---|---|
| a follow branching did not pass, the search found no values | 241 | 2,417 | Goblint 1,123, Automizer 492, CPAchecker 471, Taipan 199, Kojak 132 | 529 |
| - no term: the value depends on inputs through branches between waypoints | | 742 | Goblint 564 | 123 |
| - opaque term: nondet memory (intel-tdx), aws pointers, math functions | | 493 | CPAchecker 308 | 53 |
| - nonlinear: `*`, `/`, `%`, shifts, an input twice | | 374 | Automizer 177 | 153 |
| - `&` and `\|` of bits, which are not truth values (btor2c, xcsp) | | 285 | Automizer 113, Goblint 85 | 45 |
| - one input, simple relation, contradicted by kept clauses (Goblint skips loop iterations; CPAchecker assumes `x == 0`, then `if (x)` true) | | 234 | Goblint 200 | 80 |
| - several inputs, `?:`, deep terms, floating point | | 285 | Goblint 145 | 75 |
| - no input: Goblint skips the rounds of a `longjmp` loop | | 4 | Goblint 4 | 0 |
| the program ended before the target segment, no waypoint failed | 242 | 651 | Goblint 490 | 82 |
| - the retry with inputs 1 passed more segments, but did not confirm | | 461 | Goblint 396 | 48 |
| timeout: the run loops without reaching the next waypoint | 124 | 416 | Goblint 310, symbiotic 42 | 101 |
| the program aborted | 4 | 389 | Goblint 349 | 32 |
| - Goblint requires the branch into `abort()` | | 310 | | 1 |
| - in a guard the search does not steer (`&`, `\|`, several inputs, an input twice) | | 79 | | 31 |
| unsupported: threads | 4 | 275 | CPAchecker 266 | 0 |
| the witness does not fit: Goblint's locations (column inside a statement 330, `case` labels 22) | 4 | 352 | Goblint 352 | 6 |
| `exit` in `assume_exit_if_not`: Goblint repeats assumptions around calls (CIL splits `return f(x);`) | 242 | 321 | Goblint 321 | 0 |
| undefined behaviour: null pointer 107 (LDV), out of bounds 29 (verifythis writes `x[n]`), invalid address 5 | 4 | 141 | CPAchecker 50, Automizer 45 | 38 |
| the target segment was reached, the program ended without the error | 250 | 103 | Goblint 37, symbiotic 29, Mopsa 28 | 60 |
| the error function was called before the target (Ultimate's two branchings per evaluation of `while (counter++ < N)`) | 241 | 69 | Mopsa 22, Ultimate 42 | 7 |
| the restart limit: guards that a loop evaluates once per input, assumptions that the defaults break one by one | 4 | 47 | Goblint 38, symbiotic 8 | 26 |
| other: out of memory 50 (the interpreter's limit 49), `function_return` with rounded floats 42, endless loop without effect 27, assumption 7, rest 8 | | 134 | | 23 |

`docs/roadmap.md` ("Where NITWIT does not confirm") ranks the remedies.

### setjmp and longjmp

At 0914f93, `_setjmp` ended 93 runs as unsupported: 91 on buggy programs, all of `c/longjmp`, and Goblint's 2 on the
correct programs `04-counting-local_true` and `07-returns_true`. b146595 supports setjmp and longjmp
(`src/interp/README.md`, "Non-local jumps"). Only runs on programs that name one of their functions can change, so the
373 witnesses on such programs ran again with its archive, with the runner and limits above, 4 in parallel
(2026-10-27): the 93, and 280 on pthread, busybox, coreutils, ldv and openbsd programs whose headers declare them.
The 280 give the exit codes of 0914f93. Of the 93:

| Producer | buggy | confirmed | others |
|---|---|---|---|
| Automizer | 24 | 24 | 12; 12 have no unique SV-COMP result |
| CPAchecker | 18 | 18 | 18 |
| Goblint | 13 | 9 | 9 |
| Kojak | 9 | 9 | 9 |
| Taipan | 9 | 9 | 9 |
| symbiotic | 9 | 9 | 9 |
| test-to-witness | 9 | 9 | – |
| **total** | **91** | **87** | 66 |

- **Not confirmed:** Goblint's 4 witnesses of the `_unknown_1_pos` programs 04, 12, 15 and 16, whose check fails only
  in a later round of the longjmp loop: they put the branching into the error right after the first execution of the
  check's statement, where it does not pass (exit 241). No other validator confirms them.
- **Correct programs:** Goblint's 2 witnesses fail at a follow branching (exit 241).
- **The setjmp calls** of the 20 `c/longjmp` programs with witnesses are expression statements, initializers
  (`int count = _setjmp(env);`) and assignments (`val = _setjmp(env);`), and longjmps from `main` itself or from a
  function it calls return to all of them. The 14 other tasks of `c/longjmp`, which SV-COMP 2026 has no witnesses
  of, also use `if (setjmp(env))` and `if (0 == setjmp(env))`. `42-poison-reduced_true` asserts the value that a local
  changed before the longjmp keeps (`src/interp/README.md`, "Non-local jumps"), but reads a local of a returned
  function first, which ends every run on it as undefined (exit 4). `19-simpler_true` and `20-simpler-multifun_true`
  jump back for ever, as natively, so that a run on them ends at the time limit. A witness with the target only
  (`reach_error` in `__VERIFIER_assert`), with `-32` and `-64`, is confirmed on each of the 20 buggy tasks and on none
  of the 14 correct ones.
- **Runtime:** each of the 93 runs takes less than 10 ms of CPU time and at most 6 MB.

### Runtime

- **CPU time:** 18.6 h in all, 97 % of it in the 694 timeouts (2.5 % of the runs; Goblint 571). The 27,235 runs that
  end without a timeout or running out of memory take 1,846 s (median 0.00 s, 90th percentile 0.09 s); the 9,637
  confirmations 1,068 s. Wall time 3.8 h at 5 in parallel.
- **Out of memory:** 64 runs; 61 exceed the interpreter's limits (4 GiB of live objects, or the 32-bit address
  space), 3 NITWIT's own memory (the `forester-heap` witnesses below).
- **Memory:** max RSS median 6.0 MB, 90th percentile 21 MB, 99th percentile 842 MB. 5 runs exceed 1 GB, up to 4.5 GB:
  - three Goblint `forester-heap` witnesses whose endless input loop allocates until NITWIT's own memory runs out
    (4.5 GB);
  - two test-to-witness witnesses with 400,001 and 600,001 segments, confirmed at 1.4 and 2.0 GB.
- **No crashes.**

## What limits the comparison

- **Not BenchExec:** one run per witness, without cgroups or CPU pinning; the time limit is wall time (SV-COMP limits
  CPU time, about the same for a single-threaded tool), and the memory limit is address space (`ulimit -v`), not
  resident memory. A 32-bit 3.x run has at most 4 GB of address space anyway.
- **The other validators** ran in SV-COMP 2026, under its BenchExec setup, not on this machine.
- **Load:** 3.x and NITWIT 4 ran at different times on the same machine with 5 parallel runs each; other jobs shared the
  machine at times. Most runs are below the 10 ms resolution of the CPU time.
- **Ground truth:** a confirmation counts as right when the task is buggy, as in SV-COMP. Whether a witness is valid
  is known only for the crafted witnesses.
- **Witnesses:** only unreach-call and format 2.x, since NITWIT 4 does not support GraphML; 3,601 witnesses whose
  program is not in the dataset are missing. The 1000-witness sample is stratified by producer (100 each), not
  weighted like SV-COMP's witnesses.

## Reproducing

In WSL, with the repository at `<checkout>` and an sv-benchmarks checkout at `<sv-benchmarks>`:

```bash
D=/mnt/e/workspace/playground/svcomp-2026-witnesses; M=~/mirror; R=~/results
<checkout>/tools/package.sh ~/nitwit-4.0 && unzip -q -d ~/nitwit-4.0 ~/nitwit-4.0/nitwit-*.zip
git -C <checkout> archive --prefix=nitwit-3.x/ 8e093bf | tar -x -C ~ && (cd ~/nitwit-3.x && ./build.sh)
N3=~/nitwit-3.x/nitwit.sh; N4=~/nitwit-4.0/nitwit/nitwit.sh
python3 -I $D/scripts/sample.py $D unreach-call-100-per-producer 100
python3 -I $D/scripts/sample.py $D unreach-call-all all <sv-benchmarks>
S=$M/samples/unreach-call-100-per-producer.tsv; A=$M/samples/unreach-call-all.tsv
bash $D/scripts/mirror_sample.sh $D $D/samples/unreach-call-100-per-producer.tsv $M
bash $D/scripts/mirror_sample.sh $D $D/samples/unreach-call-all.tsv $M
bash $D/scripts/run_sample_sh.sh $M $S $R/3.x $N3 && bash $D/scripts/run_sample_sh.sh $M $S $R/4.0 $N4
python3 -I $D/scripts/compare_runs.py $S $R/3.x $R/4.0 3.x 4.0 90 comparison.tsv
bash $D/scripts/run_sample_sh.sh $M $A $R/all $N4
```

`scripts/run_sample_sh.sh` uses SV-COMP's limits by default (90 s, 5 runs in parallel, 8 GB of address space).
`build.sh` of 3.x ends with an error once `bin/` is complete (see "Binaries"); its `nitwit.sh` and `bin/` are what the
runs need. `ctest --preset <p> -L corpus` validates `validation-crafted` and `testfiles` with a build's `nitwit`.
