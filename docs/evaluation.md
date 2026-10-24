# NITWIT 4: evaluation against NITWIT 3.x

NITWIT 4.0 and NITWIT 3.x as SV-COMP violation-witness validators on 1000 unreach-call witnesses of SV-COMP 2026, at
SV-COMP's 90 s.

> **FRESH-RUN placeholder (summary):** the summary of the run of the release build goes here, with its date. Until
> then, the numbers below are those of the current code (a997479) measured on 2026-10-23, before the release build.

- **Confirmations:** 4.0 confirms 587 of the 851 witnesses of buggy programs, 3.x 312. 4.0 loses 4 confirmations of
  3.x (the two crafted `while.3*` witnesses, CPAchecker's per-call assumptions and `systemc/toy1`, see "Lost
  confirmations").
- **Wrong confirmations:** none by either tool on the 147 witnesses of correct programs, and none of Paulina's 59
  invalid crafted witnesses.
- **Runtime:** on the 903 witnesses both tools decide (no timeout, no limit of either tool), 4.0 takes 27 s of CPU time
  and 3.x 348 s; on the 800 of them that both decide within 0.05 s, 2.3 s and 3.5 s. Timeouts: 4.0 12, 3.x 3. No run
  of 4.0 takes more than 1 GB.

## Method

- **Sample:** `samples/unreach-call-100-per-producer.tsv` of the SV-COMP 2026 witness dataset
  (`/mnt/e/workspace/playground/svcomp-2026-witnesses`, see its `README.md`), drawn by
  `scripts/sample.py <dataset> unreach-call-100-per-producer 100`: 100 unreach-call violation witnesses in format 2.x
  per producer (seed 2027), 1000 in all, on 789 distinct programs; 738 run with `-32`, 262 with `-64`; format 2.0: 710,
  2.1: 290 (symbiotic 90, test-to-witness and witch all). 78 of them are also in the 250-witness sample of the corpus
  test (`tests/corpus/validate/`). The expected verdict is that of the task's unreach-call property (sv-benchmarks tag
  `svcomp26`): 851 `false` (buggy program), 147 `true` (correct program), 2 `unknown` (ldv tasks without an
  unreach-call verdict).
- **Paulina's witnesses** are the crafted witnesses of `c/validation-crafted`; `scripts/crafted_map.py` matches each
  to one with the same violation sequence: 41 valid (must be confirmed), 59 invalid (must be rejected), all on buggy
  programs.
- **Binaries:**
  - NITWIT 4.0.0: `nitwit-4.0.0.zip` built by `tools/package.sh` (GCC 14, preset `release`, libstdc++ and libgcc
    static), unpacked; `nitwit/nitwit.sh`. **FRESH-RUN placeholder:** the source revision of the release build, as
    `licenses/VERSIONS` in the archive names it.
  - NITWIT 3.x: the last commit of the branch `old`, 8e093bf, exported with `git archive`, built with its `build.sh`
    (Release, GCC 13.3; `make -j4` instead of `-j8`; its last step, the submission archive, fails for lack of
    `dos2unix` after `bin/` is complete); its `nitwit.sh`, `bin/nitwit32` (32-bit) and `bin/nitwit64`.
- **Command line:** BenchExec's order, `nitwit.sh -p <prp> -32|-64 -w <witness> <program>`, from the directory of
  `nitwit.sh` (3.x's calls `./bin/nitwit$bits`). `<prp>` is sv-benchmarks' `c/properties/unreach-call.prp` (tag
  `svcomp26`), byte for byte the archive's `smoketest/unreach-call.prp`.
- **Program names:** both tools run the program under its original file name, a symbolic link
  `programs/<witness>/<name>`, `<name>` being the last path component of `programfile` in the witness's metadata (a
  plain name for all 1000), because NITWIT 4 rejects waypoints whose `location.file_name` names another file. No
  4.0 run failed for a file name. On the 78 witnesses shared with the 250-witness sample, whose 3.x results come from
  runs under hash names with `bin/nitwit$bits` called directly, 3.x gives the same exit code on all 78.
- **Limits:** `timeout --kill-after=5 90` and `ulimit -v 8000000` (8 GB of address space) per run; 5 runs in parallel,
  one tool at a time, 3.x first. Under WSL2, `timeout` ends busy runs late: the timeouts ended after 93 to 96 s.
- **Measurement:** `/usr/bin/time -f "%e %U %S %M"` around `timeout`: wall time, user + system CPU time and maximum
  RSS, children included. The inputs were copied to WSL's own file system first (`scripts/mirror_sample.sh`).
  3.x's `nitwit.sh` does not `exec` its executable: on a timeout it dies before reaping `bin/nitwit32`, whose CPU time
  and memory are then missing. Its 3 timeouts were therefore measured again with `bin/nitwit32` called directly, same
  limits (`remeasured.tsv`: 95.2 to 95.3 s CPU, up to 1.6 GB).
- **Machine:** WSL2 Ubuntu 24.04, AMD Ryzen 9 7900X (12 cores, 24 threads), 30 GB; load average 2 to 6 during the
  runs (`run.info`).
- **Scripts and results** (in the dataset): `scripts/run_sample_sh.sh` runs a tool, `scripts/compare_runs.py` makes
  the tables below; `results/2026-10-22-nitwit-3.x-unreach-call-100-per-producer` holds 3.x's `results.tsv`, logs and
  `run.info`. **FRESH-RUN placeholder:** the results directory of the release build.

## Confirmations

> **FRESH-RUN placeholder:** the numbers of this section are from an older build (3450903) and will be replaced by
> those of the release build.

Witnesses of buggy programs confirmed (exit 0), and the transitions per witness:

| Producer | buggy | 3.x | 4.0 | lost | gained |
|---|---|---|---|---|---|
| Automizer | 99 | 9 | 70 | 0 | 61 |
| CPAchecker | 100 | 45 | 76 | 1 | 32 |
| Goblint | 27 | 2 | 3 | 0 | 1 |
| Kojak | 99 | 19 | 60 | 0 | 41 |
| Mopsa | 100 | 50 | 68 | 8 | 26 |
| Paulina (41 valid, 59 invalid) | 100 | 18 | 37 | 2 | 21 |
| Taipan | 99 | 14 | 62 | 0 | 48 |
| symbiotic | 100 | 75 | 87 | 1 | 13 |
| test-to-witness | 27 | 20 | 27 | 0 | 7 |
| witch | 100 | 60 | 87 | 0 | 27 |
| **total** | 851 | **312** | **577** | 12 | 277 |
| **without Paulina** | 751 | **294** | **540** | 10 | 256 |

- **Correct programs** (Goblint 72, test-to-witness 73, Kojak 1, Taipan 1): 0 confirmed by either tool. 3.x crashes
  on 5 of them (heap corruption, segmentation fault; BenchExec: ERROR), 4.0 on none.
- **Paulina:** 3.x confirms 18 of the 41 valid crafted witnesses, 4.0 37; neither confirms an invalid one. The 4 valid
  ones 4.0 misses: `while.3A1`, `while.3B1` (lost, below), `switch.4A1`, `switch.4B1` (exit 250; 3.x: 242).
- **Unknown expected verdict** (2): neither tool confirms.
- **For SV-COMP** (confirmations count where the task is buggy): 4.0 577, 3.x 312; wrong confirmations: 0 and 0.

## Lost confirmations

> **FRESH-RUN placeholder:** the numbers of this section are from an older build (3450903) and will be replaced by
> those of the release build. With the current code, 4.0 confirms the target-only witnesses below except
> `systemc/toy1`, and loses 4 confirmations of 3.x: `systemc/toy1`, the two crafted `while.3*` witnesses and
> CPAchecker's `minepump_spec3_productSimulator`.

All 12 witnesses that 3.x confirms and 4.0 does not, classified from the logs and runs with `-v`. All are on buggy
programs, and the two crafted ones are valid witnesses, so each is a confirmation lost, not a soundness problem.

- **Target-only witnesses (9: Mopsa 8, symbiotic 1).** The witness has one waypoint, the target. 3.x reaches the error
  with its input values (effectively 1, `src/validate/README.md`, "Defaults"); 4.0 gives inputs 0, and without a
  waypoint the search has no requirement to solve ("Limits of the search"):
  - the target segment is current, but `main` returns without the error (exit 250): `hard2_unwindbound1`
    (`2837e917cfd5`), `hard2_unwindbound2` (`e0f034f55d55`), `bresenham-ll_unwindbound1` (`b0c88dd98e39`),
    `cast_union_tight` (`6c0f4cbaf25b`), `for_bounded_loop1` (`d72c3efbec94`), symbiotic's
    `BallRajamani-SPIN2000-Fig1` (`a21c509ef84d`);
  - `assume_abort_if_not` aborts on input 0 (exit 4): `hard-u_unwindbound1` (`4cc46515ce48`), `hard-ll_unwindbound1`
    (`fd4f6a6c26f6`);
  - `systemc/toy1` (`aadb77432e42`): the run with inputs 0 does not end, and NITWIT's own memory runs out after 27 s
    (exit 251).
- **Paulina `while.3A1`, `while.3B1` (`796583053ff3`, `eba394825e26`), valid crafted witnesses.** The assumption
  `a == 10` at `count++;` (line 25) needs `n = 90` in `if (n > 0) a = a - n;`, which no waypoint covers. With n = 0 the
  assignment does not run, so `a` depends on `n` only through control flow and its term is the hinted input (100):
  "needs `in0 == 10`: no input values found" (exit 241; known gap "values that depend on inputs only through control
  flow").
- **CPAchecker `minepump_spec3_productSimulator` (`2fb20af996b7`).** CPAchecker writes one segment per call of
  `select_one`, each an assumption at the same location (the `{` at 952:3) with another value: `choice == 0`, then
  `choice == 2`, and so on. 4.0's cascading rule (`src/validate/README.md`, "Cascading") evaluates the next segment's
  assumption at the same execution of the statement, so `choice == 2` would have to hold together with
  `choice == 0`: "needs `in0 == 2`: no input values found" (exit 241). The witness means the next call; 3.x reads it
  that way. The rule serves CPAchecker's repeated identical assumptions (often `1`) at one location; this witness needs
  the other reading. One witness in 1000.

## Why 4.0 does not confirm the other buggy witnesses

> **FRESH-RUN placeholder:** the numbers of this section are from an older build (3450903) and will be replaced by
> those of the release build.

The 211 unconfirmed witnesses of buggy programs without Paulina, by exit code and result line:

| Reason | Exit | Witnesses | Producers |
|---|---|---|---|
| left the witness: a follow `branching` did not pass, the search found no values | 241 | 89 | Taipan 24, Automizer 22, Kojak 22, Goblint 11, CPAchecker 10 |
| left the witness: a follow `assumption` did not pass | 241 | 2 | CPAchecker 2 |
| the error function was called before the target | 241 | 14 | Mopsa 8, Kojak 3, Taipan 3 |
| the program ended before the target segment (`main` returned 20, `exit` 5) | 242 | 25 | Goblint 9, Kojak 7, Taipan 5, Automizer 3, CPAchecker 1 |
| the target segment was reached, the program ended without the error | 250 | 18 | Mopsa 12, symbiotic 5, witch 1 |
| unsupported: threads (`pthread_create` 7, `pthread_key_create`, `pthread_mutex_init`) | 4 | 9 | CPAchecker 9 |
| unsupported: `_setjmp` | 4 | 4 | Kojak 2, Automizer 1, CPAchecker 1 |
| the program aborted (`abort`, `assume_abort_if_not`) | 4 | 11 | Mopsa 9, symbiotic 1, witch 1 |
| undefined behaviour: null pointer read 9, invalid address 1, write out of bounds 1 | 4 | 11 | Kojak 5, Taipan 4, Automizer 2 |
| the witness does not fit: Goblint's assumptions at column 6 of `    (*dest).fcw = ...;`, inside the statement | 4 | 3 | Goblint 3 |
| the witness cannot be parsed (`line: 0`, rejected by 3.x too) | 2 | 1 | Mopsa 1 |
| out of memory: the program allocates more than the interpreter's limit (`malloc` of about 2^55 bytes, aws harnesses) | 251 | 12 | witch 8, symbiotic 4 |
| out of memory: NITWIT's own memory (`std::bad_alloc` at about 3.9 GB) | 251 | 6 | Automizer, CPAchecker, Goblint, Mopsa, Taipan, symbiotic |
| timeout | 124 | 6 | witch 3, symbiotic 2, Mopsa 1 |

Paulina's 63 unconfirmed witnesses are the 59 invalid ones (rejected: a follow waypoint failed 17, an avoid waypoint
passed 13, the program ended 11 before and 13 in the target segment, the error function was called before the target
5) and the 4 valid ones above.

3.x's 539 unconfirmed buggy witnesses, for comparison: the program ended (242) 169, front-end and interpreter errors
of PicoC 190 (`';' expected` 92, `identifier not expected here` 38, ...), the transition limit 67, the error function
called before the target (252) 58, the program aborted 27, other exit 4 24, timeouts 3, the witness rejected
(`line: 0`) 1.

## Runtime

The runtime is compared on the witnesses both tools decide: the runs that end without a timeout or a limit of either
tool. A timeout gives no answer either way, and NITWIT ends a run early only where the witness can no longer be
followed (`docs/exit-codes.md`).

> **FRESH-RUN placeholder:** the numbers of this section are from the current code (a997479, measured on 2026-10-23:
> the first table and the paragraph after it) and from an older build (3450903, measured on 2026-10-22: the second
> table and the notes after it); all will be replaced by those of the release build.

| | witnesses | 3.x | 4.0 |
|---|---|---|---|
| both decide (no timeout, no limit, 3.x not at its transition limit) | 903 | 348 s | 27 s |
| - of them both at most 0.05 s (front end and start-up dominate) | 800 | 3.5 s | 2.3 s |
| 3.x stops at its limit of 5 M transitions, 4.0 ends (2 of them confirmed) | 70 | 192 s | 115 s |

4.0 is slower on 77 of the 973 witnesses both runs finish: on 72 by less than a second, mostly where 3.x stops at a
parse error and 4.0 runs the program; on 5 by more, among them two long confirmations of Mopsa (61 s and 49 s, see
below) and two confirmations that need restarts of the search (0.4 to 1.6 s, 0.5 to 1.1 s).

CPU time (user + system) and memory per run, all 1000 witnesses:

| | 3.x | 4.0 |
|---|---|---|
| CPU time, sum | 832 s | 1061 s |
| - runs that ended (exit code of the tool) | 546 s | 172 s |
| - of them confirmations | 269 s (312 runs) | 146 s (577 runs) |
| - timeouts | 286 s (3) | 566 s (6) |
| - out of memory | – | 322 s (21: 9 NITWIT's own memory, 12 the interpreter's limit) |
| CPU time per run: median, Q3, max | 0.00, 0.01, 95.3 s | 0.00, 0.00, 94.6 s |
| runs over 10 s | 14 | 17 |
| wall time of the whole run, 5 in parallel | 218 s | 234 s |
| max RSS: median, 90th percentile, max | 5.2 MB, 8 MB, 1.6 GB | 5.6 MB, 12 MB, 3.9 GB |
| runs over 1 GB RSS | 2 | 11 |
| crashes (signals) | 5 | 0 |

- **Witnesses both tools finish** (970, neither a timeout nor out of memory): 3.x 540 s, 4.0 154 s. Most runs take
  less than the 10 ms resolution of the CPU time in both tools (per-witness ratio 4.0/3.x: median 1, quartiles 1 and
  1, 4.0 faster on 181, slower on 25). On the 31 witnesses where one of them takes at least 1 s, the ratio has quartiles
  0.003, 0.004 and 0.010; 4.0 is faster on 29. It is slower on 2, both confirmations where 3.x stops at its limit of
  5 million unsuccessful witness transitions after 0.6 to 0.7 s (exit 4): Mopsa's `loop-acceleration/simple_2-2`
  (75 s) and `loops-crafted-1/nested3-2` (63 s).
- **Where 4.0 hits a limit** (15 runs, 888 s): 6 timeouts (three ldv-linux-4.0-rc1-mav programs of witch, two btor2c
  programs of symbiotic at 2.1 and 3.8 GB, Mopsa's `loop-acceleration/nested_1-2`) and 9 runs that exhaust NITWIT's
  own memory after 23 to 59 s at about 3.9 GB RSS (ldv, systemc, product-lines, combinations). 3.x stops on 14 of them
  within 3.3 s (exit 4): 9 with a PicoC front-end error, 5 at its transition limit (NITWIT 4 sets no step limit,
  `docs/exit-codes.md`); it confirms the 15th, `systemc/toy1` (lost confirmations above). The constant 3.9 GB is
  probably the 8 GB address-space limit: the next doubling of a growing table no longer fits. Under BenchExec, which
  limits resident memory, these runs would grow further and end out of memory or in a timeout.
- **3.x's runs over 10 s** are 3 timeouts (Mopsa's 65 MB witnesses, at 1.6 GB, and a Taipan witness) and long runs that
  4.0 finishes faster, e.g. a Goblint witness that 3.x confirms in 42.5 s and 4.0 in 0.14 s.

## What limits the comparison

- **Not BenchExec:** one run per witness and tool, without cgroups or CPU pinning; the time limit is wall time (SV-COMP
  limits CPU time, about the same for a single-threaded tool), and the memory limit is address space (`ulimit -v`),
  not resident memory. A 32-bit 3.x run has at most 4 GB of address space anyway.
- **Load:** the tools ran one after the other on the same machine with 5 parallel runs each; another job shared the
  machine. Most runs are below the 10 ms resolution of the CPU time.
- **Ground truth:** a confirmation counts as right when the task is buggy, as in SV-COMP. Whether a witness is valid
  is known only for Paulina's crafted witnesses.
- **Sample:** stratified by producer (100 each), not weighted like SV-COMP's witnesses; only unreach-call and format
  2.x, since NITWIT 4 does not support GraphML.

## Reproducing

In WSL, with the repository at `<checkout>`:

```bash
D=/mnt/e/workspace/playground/svcomp-2026-witnesses; M=~/mirror; S=unreach-call-100-per-producer.tsv
<checkout>/tools/package.sh ~/nitwit-4.0 && unzip -q -d ~/nitwit-4.0 ~/nitwit-4.0/nitwit-4.0.0.zip
git -C <checkout> archive --prefix=nitwit-3.x/ 8e093bf | tar -x -C ~ && (cd ~/nitwit-3.x && ./build.sh)
python3 -I $D/scripts/sample.py $D unreach-call-100-per-producer 100
bash $D/scripts/mirror_sample.sh $D $D/samples/$S $M
bash $D/scripts/run_sample_sh.sh $M $M/samples/$S ~/results/3.x ~/nitwit-3.x/nitwit.sh 90 5 8000000
bash $D/scripts/run_sample_sh.sh $M $M/samples/$S ~/results/4.0 ~/nitwit-4.0/nitwit/nitwit.sh 90 5 8000000
python3 -I $D/scripts/compare_runs.py $M/samples/$S ~/results/3.x ~/results/4.0 3.x 4.0 90 comparison.tsv
```

`build.sh` of 3.x ends with an error once `bin/` is complete (see "Binaries"); its `nitwit.sh` and `bin/` are what the
runs need.
