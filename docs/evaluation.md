# NITWIT 4: evaluation

NITWIT 4.0.0, the release archive built from `main` at ad6f5b502146, as an SV-COMP violation-witness validator on the
unreach-call witnesses of SV-COMP 2026, at SV-COMP's 90 s (2026-10-24).

- **All witnesses:** of the 27,767 unreach-call witnesses in format 2.x whose program is in the dataset, NITWIT confirms
  9,239 of the 14,841 witnesses of buggy programs (62 %; 78 % without Goblint's) and none of the 12,792 witnesses of
  correct programs. `validation-crafted`: 96 of 100 right, no invalid witness confirmed. `testfiles`: all 45
  format 2.x witnesses right.
- **Against NITWIT 3.x,** on 1000 of these witnesses (100 per producer): 4.0 confirms 587 of the 851 witnesses of
  buggy programs, 3.x 312; neither confirms a wrong one. On the 903 witnesses both decide, 4.0 takes 20 s of CPU time,
  3.x 348 s.
- **Against the other SV-COMP 2026 validators,** on the 11,738 witnesses of buggy programs they validated: NITWIT
  confirms 6,820, more than any of them alone (Witch 5,392), the five together 7,934. Validators outside the
  producer's family confirm 1,088 that NITWIT does not; NITWIT confirms 486 that no other validator does. NITWIT
  confirms no witness of a correct program, CPAchecker 4 and MetaVal 3.
- **Memory:** no run on the 1000 witnesses takes more than 1 GB (at most 842 MB). Of all witnesses, 5 runs do, up to
  4.6 GB ("Runtime" of "All witnesses").

## Method

Common to both runs:

- **Binaries:**
  - NITWIT 4.0.0: `nitwit-4.0.0.zip` built by `tools/package.sh` from `main` at ad6f5b502146 (GCC 14, preset
    `release`, libstdc++ and libgcc static), unpacked; `nitwit/nitwit.sh`.
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
- **Runs:** 4.0's rows are those of the run over all witnesses. 3.x's results are those of 2026-10-22
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
  must be confirmed ran again with `-v`; the reasons below come from the result lines and these logs.
- **Other validators:** the verdicts of SV-COMP 2026 (tables `validation-of-violation-witnesses-v2`) of CPAchecker,
  MetaVal, Theta, UAutomizer and Witch; Theta gave no verdict on these tasks, and the witnesses of test-to-witness and
  witch were not validated. A validator is *outside the producer's family* unless it is UAutomizer for the Ultimate
  tools (Automizer, Taipan, Kojak), CPAchecker for CPAchecker, or Witch for symbiotic.

## Against NITWIT 3.x

Witnesses of buggy programs confirmed (exit 0), and the changes per witness:

| Producer | buggy | 3.x | 4.0 | lost | gained |
|---|---|---|---|---|---|
| Automizer | 99 | 9 | 71 | 0 | 62 |
| CPAchecker | 100 | 45 | 76 | 1 | 32 |
| Goblint | 27 | 2 | 3 | 0 | 1 |
| Kojak | 99 | 19 | 60 | 0 | 41 |
| Mopsa | 100 | 50 | 76 | 1 | 27 |
| Paulina (41 valid, 59 invalid) | 100 | 18 | 37 | 2 | 21 |
| Taipan | 99 | 14 | 62 | 0 | 48 |
| symbiotic | 100 | 75 | 88 | 0 | 13 |
| test-to-witness | 27 | 20 | 27 | 0 | 7 |
| witch | 100 | 60 | 87 | 0 | 27 |
| **total** | 851 | **312** | **587** | 4 | 279 |
| **without Paulina** | 751 | **294** | **550** | 2 | 258 |

- **Correct programs** (Goblint 72, test-to-witness 73, Kojak 1, Taipan 1): 0 confirmed by either tool. 3.x crashes
  on 5 runs (heap corruption, segmentation fault; BenchExec: ERROR), 4.0 on none.
- **Paulina:** 3.x confirms 18 of the 41 valid crafted witnesses, 4.0 37; neither confirms an invalid one. The 4 valid
  ones 4.0 misses: `while.3A1`, `while.3B1` (lost, below), `switch.4A1`, `switch.4B1` (exit 250; 3.x: 242).
- **Unknown expected verdict** (2): neither tool confirms.

### Lost confirmations

The 4 witnesses that 3.x confirms and 4.0 does not are on buggy programs, and the two crafted ones are valid
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

| | witnesses | 3.x | 4.0 |
|---|---|---|---|
| both decide (no timeout, no limit, 3.x not at its transition limit) | 903 | 348 s | 20 s |
| - of them both at most 0.05 s (front end and start-up dominate) | 820 | 3.9 s | 1.9 s |
| 3.x stops at its limit of 5 M transitions, 4.0 ends (4.0 confirms 56) | 70 | 192 s | 116 s |
| all 1000 (timeouts: 3.x 3 with 286 s, 4.0 12 with 1,113 s) | 1000 | 832 s | 1,264 s |

- **Per witness:** of the 973 witnesses both runs finish, 4.0 is faster on 187, equally fast on 750 (most runs take
  less than the 10 ms resolution of the CPU time) and slower on 36. It is slower by more than 1 s on 2, both
  confirmations where 3.x stops at its transition limit after 0.6 to 0.7 s (exit 4): Mopsa's
  `loop-acceleration/simple_2-2` (60 s) and `loops-crafted-1/nested3-2` (54 s).
- **Timeouts and out of memory:** 4.0's 12 timeouts are ldv, systemc, product-lines, combinations and btor2c programs
  and Mopsa's `loop-acceleration/nested_1-2`; on 11 of them 3.x stops within 3.3 s (exit 4), and it confirms
  `systemc/toy1`. 4.0's 12 runs out of memory are aws harnesses that allocate more than the interpreter's limit.
- **Memory:** max RSS median 5.8 MB (3.x 5.2 MB), 90th percentile 12 MB (8 MB), maximum 842 MB (1.6 GB); no run of
  4.0 over 1 GB (3.x: 2).

## All witnesses

### Confirmations

Witnesses of buggy programs, without Paulina's (37 of whose 41 valid witnesses are confirmed). "SV-COMP results": the
witnesses the other validators ran on; "others": confirmed by a validator outside the producer's family; "others
only": confirmed by such a validator and not by NITWIT.

| Producer | buggy | confirmed | SV-COMP results | others | others only |
|---|---|---|---|---|---|
| Automizer | 2,665 | 1,943 (73 %) | 1,442 | 647 | 139 |
| CPAchecker | 3,619 | 2,706 (75 %) | 3,353 | 2,934 | 272 |
| Goblint | 3,342 | 269 (8 %) | 3,342 | 637 | 381 |
| Kojak | 665 | 435 (65 %) | 665 | 368 | 67 |
| Mopsa | 208 | 147 (71 %) | 192 | 167 | 33 |
| Taipan | 885 | 553 (62 %) | 885 | 475 | 110 |
| symbiotic | 1,859 | 1,741 (94 %) | 1,859 | 1,555 | 86 |
| test-to-witness | 1,228 | 1,112 (91 %) | – | – | – |
| witch | 370 | 333 (90 %) | – | – | – |
| **total** | **14,841** | **9,239 (62 %)** | 11,738 | 6,783 | 1,088 |

### Soundness

No wrong confirmation: 0 of the 12,792 witnesses of correct programs, 0 of the 118 invalid crafted witnesses
(`validation-crafted` and Paulina's copies), 0 of the 15 invalid test witnesses (8 in format 2.x; the 7 in format 1.0
give exit 4), and none of the 34 witnesses without verdict. On correct programs, most runs end because the program
aborts (7,104), a follow branching fails (1,918) or the witness does not fit the program (1,871).

### Against the other validators

The 21,647 witnesses with SV-COMP 2026 results (`validation-of-violation-witnesses-v2.C.unreach-call.*`) and a NITWIT
run: 11,738 on buggy programs, 9,909 on correct ones.

| Validator | buggy confirmed | % | wrong (correct program confirmed) |
|---|---|---|---|
| NITWIT 4.0 | 6,820 | 58.1 % | 0 |
| Witch | 5,392 | 45.9 % | 0 |
| CPAchecker | 4,906 | 41.8 % | 4 |
| UAutomizer | 4,452 | 37.9 % | 0 |
| MetaVal | 2,858 | 24.3 % | 3 |
| Theta | 0 | 0 % | 0 |
| any of the five | 7,934 | 67.6 % | 5 |

NITWIT confirms more than any single validator, the five together confirm 1,114 more, and NITWIT makes no wrong
confirmation where CPAchecker and MetaVal make 4 and 3. Counting only validators outside the producer's family, the
five confirm 6,783. 1,600 witnesses are confirmed by another validator and not by NITWIT, 1,088 of them outside the
producer's family; 486 are confirmed by NITWIT only.

### Why NITWIT does not confirm the other 5,602

"Others": confirmed by a validator outside the producer's family.

| Reason | Exit | Witnesses | Producers | Others |
|---|---|---|---|---|
| a follow branching did not pass, the search found no values | 241 | 2,570 | Goblint 1,123, Automizer 546, CPAchecker 517, Taipan 227, Kojak 157 | 622 |
| - no term: the value depends on inputs through branches between waypoints | | 730 | Goblint 552 | 126 |
| - opaque term: nondet memory (intel-tdx 268), aws pointers 120, math functions 79 | | 536 | CPAchecker 327 | 76 |
| - nonlinear: `*`, `/`, `%`, shifts, an input twice | | 410 | Automizer 199 | 159 |
| - `&` and `\|` of truth values (btor2c, xcsp) | | 283 | Automizer 113, Goblint 85 | 44 |
| - one input, simple relation, contradicted by kept clauses (Goblint skips loop iterations; CPAchecker assumes `x == 0`, then `if (x)` true) | | 261 | Goblint 214 | 93 |
| - several inputs, `?:`, deep terms, floating point | | 350 | Goblint 144 | 124 |
| the program ended before the target segment, no waypoint failed | 242 | 663 | Goblint 491 | 88 |
| - the retry with inputs 1 passed more segments, but does not search | | 464 | Goblint 389 | 52 |
| the program aborted | 4 | 479 | Goblint 409 | 87 |
| - Goblint requires the branch into `abort()` | | 304 | | 1 |
| - in an `assume` wrapper (`assume_abort_if_not`, `assume`; xcsp constraints) | | 174 | | 85 |
| timeout: the run loops without reaching the next waypoint | 124 | 416 | Goblint 311, symbiotic 42 | 100 |
| unsupported: threads 275 (CPAchecker 266), `_setjmp` 91 | 4 | 366 | CPAchecker 284 | 66 |
| the witness does not fit: Goblint's locations (column inside a statement 330, `case` labels 22) | 4 | 352 | Goblint 352 | 6 |
| `exit` in `assume_exit_if_not`: Goblint repeats assumptions around calls (CIL splits `return f(x);`) | 242 | 325 | Goblint 323 | 0 |
| undefined behaviour: null pointer 107 (LDV), out of bounds 28 (verifythis writes `x[n]`), invalid address 5 | 4 | 140 | CPAchecker 50, Automizer 45 | 38 |
| the target segment was reached, the program ended without the error | 250 | 86 | Goblint 37, symbiotic 29 | 46 |
| the error function was called before the target (Ultimate's two branchings per evaluation of `while (counter++ < N)`) | 241 | 65 | Mopsa 22, Ultimate 42 | 7 |
| other: out of memory 50 (the interpreter's limit 49), `function_return` with rounded floats 42, endless loop without effect 27, assumption 12, rest 9 | | 140 | | 28 |

`docs/roadmap.md` ("Where NITWIT does not confirm") ranks the remedies.

### Runtime

- **CPU time:** 18.4 h in all, 97 % of it in the 695 timeouts (2.5 % of the runs; Goblint 573). The 27,235 runs that
  end without a timeout or running out of memory take 2,045 s (median 0.00 s, 90th percentile 0.07 s); the 9,350
  confirmations 1,255 s. Wall time 4.03 h at 5 in parallel.
- **Out of memory:** 63 runs; 61 exceed the interpreter's limits (4 GiB of live objects, or the 32-bit address
  space), 2 NITWIT's own memory.
- **Memory:** max RSS median 5.8 MB, 90th percentile 21 MB, 99th percentile 842 MB. 5 runs exceed 1 GB, up to 4.6 GB:
  - three Goblint `forester-heap` witnesses: two whose endless input loop allocates until NITWIT's own memory runs out
    (4.6 GB), and one that times out at 2.4 GB;
  - two test-to-witness witnesses with 400,001 and 600,001 segments, confirmed at 1.4 and 2.1 GB.
- **No crashes.**

## What limits the comparison

- **Not BenchExec:** one run per witness, without cgroups or CPU pinning; the time limit is wall time (SV-COMP limits
  CPU time, about the same for a single-threaded tool), and the memory limit is address space (`ulimit -v`), not
  resident memory. A 32-bit 3.x run has at most 4 GB of address space anyway.
- **The other validators** ran in SV-COMP 2026, under its BenchExec setup, not on this machine.
- **Load:** 3.x and 4.0 ran at different times on the same machine with 5 parallel runs each; other jobs shared the
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
<checkout>/tools/package.sh ~/nitwit-4.0 && unzip -q -d ~/nitwit-4.0 ~/nitwit-4.0/nitwit-4.0.0.zip
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
