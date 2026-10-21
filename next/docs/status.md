# NITWIT 4: status

| Stage | Status | Numbers |
|---|---|---|
| 0 Foundation | done | `support`: 19 unit tests; GCC 14, Clang 20 and ASan/UBSan clean |
| 1 Lexer | done and reviewed | all 19,752 corpus and test files: 628 M tokens, 0 errors, 0 decoding failures; 38 files need the preprocessor; 135 MB/s per file |
| 2 Parser | done and reviewed | all 19,714 corpus and test files without directives parse (100 %); the 34 programs that need `cpp` parse after `gcc -E` (68/68 with -m32/-m64); 115 MB/s; 244 unit tests, 22 golden dumps; 4.9 M fuzz runs without findings |
| 3 Sema | done and reviewed | 19,703 of 19,714 parsed corpus programs analyzed (99.944 %; the 11 others use `__int128` on 32 bit, which GCC rejects too); layout and constant probes against GCC: 589,000 checks on 900 corpus programs and a type zoo, 0 mismatches except glibc's `max_align_t` with `__float128` on i386; type-and-value zoo checked by GCC and sema in both data models; corpus warnings grouped (4,014 in 480 files) |
| 4 Interpreter | `value` and `Memory` done and reviewed; C library done and reviewed (glibc-identical printf, strings, ctype, math, builtins; tested against native probes in both data models); interpreter core in progress; difftest tools and the semantics suite done and reviewed | `value`: 234 tests, 0 mismatches in 1.5 M random probes against GCC; `Memory`: 100 tests, 0 mismatches against a per-byte reference model; semantics suite: 50 programs (incl. 6 harness contract tests), identical under GCC/Clang, -m32/-m64, with sanitizers; GCC and Clang agree on 421 of 500 corpus programs (the rest: compile failures, limits, sanitizer findings, 1 uninitialized read) |
| 5 Witness models | done and reviewed | 58,093 of 58,119 corpus witnesses parse (the 26 others have `line: 0`); all 111 test witnesses; largest witness (235 MB): 12.6 s, 1.5 GB; 81 unit tests; fuzzed |
| 6 Validation 2.x | constraint parsing done (34 tests); monitor, oracle and restarts in progress | |
| 7 Value search | – | |
| 8 GraphML | – | |
| 9 Release | – | |
