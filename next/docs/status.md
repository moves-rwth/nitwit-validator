# NITWIT 4: status

| Stage | Status | Numbers |
|---|---|---|
| 0 Foundation | done | `support`: 19 unit tests; GCC 14, Clang 20 and ASan/UBSan clean |
| 1 Lexer | done, review running | all 19,752 corpus and test files: 628 M tokens, 0 errors, 0 decoding failures; 38 files need the preprocessor; 135 MB/s per file |
| 2 Parser | in progress (`ast` and `parse`) | |
| 3 Sema | API defined | |
| 4 Interpreter | `value` and `Memory` done and reviewed; rest of the API defined | `value`: 234 tests, 0 mismatches in 1.5 M random probes against GCC; `Memory`: 100 tests, 0 mismatches against a per-byte reference model |
| 5 Witness models | in progress | |
| 6 Validation 2.x | semantics documented | |
| 7 Value search | – | |
| 8 GraphML | – | |
| 9 Release | – | |
