# NITWIT 4: status

| Stage | Status | Numbers |
|---|---|---|
| 0 Foundation | done | `support`: 19 unit tests; GCC 14, Clang 20 and ASan/UBSan clean |
| 1 Lexer | done and reviewed | all 19,752 corpus and test files: 628 M tokens, 0 errors, 0 decoding failures; 38 files need the preprocessor; 135 MB/s per file |
| 2 Parser | done and reviewed | all 19,714 corpus and test files without directives parse (100 %); the 34 programs that need `cpp` parse after `gcc -E` (68/68 with -m32/-m64); 115 MB/s; 244 unit tests, 22 golden dumps; 4.9 M fuzz runs without findings |
| 3 Sema | in progress | |
| 4 Interpreter | `value` and `Memory` done and reviewed; C library in progress; difftest tools and 44 semantics programs done, review running | `value`: 234 tests, 0 mismatches in 1.5 M random probes against GCC; `Memory`: 100 tests, 0 mismatches against a per-byte reference model |
| 5 Witness models | done and reviewed | 58,093 of 58,119 corpus witnesses parse (the 26 others have `line: 0`); all 111 test witnesses; largest witness (235 MB): 12.6 s, 1.5 GB; 81 unit tests; fuzzed |
| 6 Validation 2.x | semantics documented | |
| 7 Value search | – | |
| 8 GraphML | – | |
| 9 Release | – | |
