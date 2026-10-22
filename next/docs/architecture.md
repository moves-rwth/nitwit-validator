# NITWIT 4: architecture

## Data flow

```
program.c/.i ──► lex ──► parse ──► sema ──► typed AST ──► interp (lowering, evaluation)
                                     ▲                         │ events: statement, condition,
witness.yml ──► witness (model) ─────┤                         │ call entry, call return, input request
                                     │                         ▼
                  constraints compiled against the program   validate (monitor, input oracle, restarts)
                                                               │
                                                             driver ──► verdict, exit code
```

## Modules and dependencies

Each module is a static library `nitwit_<module>` in `src/<module>/`. A module may only use the modules listed for it,
and CMake enforces this through the link dependencies.

| Module | Responsibility | Uses |
|---|---|---|
| `support` | source files and locations, diagnostics, target data models, string interning, assertions | – |
| `value` | run-time values, their byte representation, and C arithmetic with undefined-behaviour reporting | support |
| `lex` | tokens, lexer, literal decoding, line markers | support |
| `ast` | AST nodes, types, `AstContext` (arena, type uniquing), AST dump | support |
| `parse` | recursive-descent parser for C17 plus GNU extensions → untyped AST | lex, ast |
| `sema` | name resolution, types, layouts, constant evaluation, implicit conversions → typed AST | ast, value |
| `interp` | memory, lowering to per-function instruction lists, evaluation, builtins | ast, sema, value |
| `witness` | witness models and parsers (2.x YAML, 1.0 GraphML), no C knowledge | support |
| `validate` | monitors per witness format, constraint compilation, input oracle, restart loop, verdict | interp, witness, parse, sema |
| `driver` | command line, reading files, exit codes, output | all |

Third-party libraries come in through CMake `FetchContent`, pinned by URL and SHA-256: GoogleTest 1.17.0 (tests only),
yaml-cpp 0.8.0 (witness), pugixml 1.15 (witness).

## Key decisions

### Source locations

- A `SourceLocation` is a file id plus a byte offset. The `SourceManager` turns it into a physical line and column
  (both 1-based, a tab counts as one column), and into a *presumed* location by applying line markers
  (`# 12 "file.c"`).
- Witnesses refer to physical lines of the input file. For files that went through the external preprocessor they
  refer to presumed lines; the driver tells the validation layer which mapping to use.

### Tokens and preprocessing

- The lexer turns a whole file into a `std::vector<Token>`. It handles line markers itself. It passes each `#pragma`
  on as one `Pragma` token, and the parser applies `pack` (to record layouts) and `weak`.
- For any other directive it reports that the file needs preprocessing. The driver then runs the system `cpp`
  (`-m32`/`-m64`, line markers kept) and lexes the result.
- Columns in lines that contain macro invocations are approximate in that case. An own preprocessor that tracks
  expansion locations is a later stage.

### AST

- Nodes are plain classes with a kind enum, allocated in the arena of `AstContext` and never freed one by one. Pointers
  between nodes are raw and non-owning.
- `isa<T>`, `cast<T>` and `dyn_cast<T>` (in `ast/casting.hpp`) test the kind. Code that visits all kinds switches over
  the kind enum, so the compiler reports missing cases.
- The parser builds the *syntactic* AST: no types on expressions, names unresolved except for typedef names. It tracks
  typedef names per scope to resolve the C declaration/expression ambiguity.
- Semantic analysis completes the *same* AST in place:
  - resolves names to declarations;
  - computes the type and value category of every expression;
  - inserts `ImplicitCastExpr` nodes for every conversion: lvalue-to-rvalue, decay, integer promotion, usual arithmetic
    conversions, assignment conversions;
  - turns initializer lists into their semantic form: one entry per member or element, designators and brace elision
    resolved.
- The result is a typed AST in which every conversion is explicit. The interpreter therefore never decides a C typing
  rule itself.

### Types and target

- Types are uniqued in `AstContext`, so that equal types are equal pointers. A `QualType` is a `const Type*` plus
  `const`/`volatile`/`restrict`.
- Typedefs remain visible as `TypedefType` sugar for diagnostics and dumps. `QualType::canonical()` strips them.
- The data model (`TargetInfo`: ILP32 or LP64, x86 conventions) is fixed when the `AstContext` is created.
- Record layouts (offsets, bit-fields, `packed`, `aligned`) are computed by `sema` and stored in the `RecordDecl`.

### Interpreter

- **Memory** is a flat, byte-addressed virtual address space of 32 or 64 bits with an allocation table. Every object
  (global, local, heap block, string literal, function) is an *allocation* with a base address, size, region, liveness
  and an "initialized" bit per byte. Pointers are plain addresses; dereferencing looks the address up in the allocation
  table. This makes pointer/integer casts, `memcpy` of structs with pointers, and unions work. Out-of-bounds,
  use-after-free and the like are detected and reported as memory errors.
- **Values** are bit patterns plus a representation: integers and pointers up to 128 bits, `float`, `double`,
  `long double` (x87, as on the target), and byte blobs for structs and unions. The C type is not part of the value; it
  comes from the AST node. A value derived from nondeterministic inputs carries a *term*: the arithmetic operations
  that computed it from them (`value/README.md`, "Terms"; stage 7, before that only the input it was copied from).
  Memory keeps the terms of stored scalars in a shadow map.
- **Lowering:** each function body is lowered to a flat list of instructions:
  - `Evaluate`, `Declare` (initializer, VLA allocation), `Branch`, `Jump`, `Switch`, `Return`, `EndScope`;
  - each instruction carries its source construct.
  This makes `goto`, `switch` into nested blocks, `break` and `continue` plain jumps. Expressions stay trees and are
  evaluated recursively.
- **Calls** recurse on the host stack. The driver runs the interpreter on a thread with a large stack (1 GiB), and a
  configurable call-depth limit turns runaway recursion into an UNKNOWN result instead of a crash.
- **Nondeterminism goes through an `InputOracle`.** Each request has a key: its call site and how often that site has
  been reached. Requests come from `__VERIFIER_nondet_*`, from calls of functions without a definition, and from reads
  of uninitialized local or heap memory, which the SV-COMP verifiers also treat as nondeterministic.
- **Events go to an `ExecutionObserver`.** The interpreter reports:
  - the start of a statement;
  - a condition after it was evaluated (`if`, loops, `switch`, `?:`), with its outcome;
  - a call after its arguments were evaluated;
  - a call after it returned, with the return value.
  Each event carries the exact location the witness formats use, for example the keyword of a branching or the `)` of
  a call. While handling an event, the observer may evaluate a typed expression in the current frame, and it can ask
  the interpreter to stop.
- **Undefined behaviour** (signed overflow, invalid shifts, division by zero, memory errors) is reported through the
  observer. The default policy stops the run with UNKNOWN for operations without a defined result (memory errors,
  division by zero). Signed overflow wraps and is only reported.

### Validation

- A **monitor** per witness format consumes the events and tracks the progress through the witness:
  - format 2.x: the current segment, evaluation points, first-evaluation semantics for follow waypoints, avoid
    waypoints;
  - format 1.0: the automaton.
- **Constraints** are C expressions. They are parsed with the C parser and checked by `sema` in the scope that is
  visible at the waypoint's location. `\result` refers to the return value of the call being observed.
- **The input oracle** answers requests in this order:
  1. inputs fixed by earlier runs;
  2. hints from the monitor, e.g. a `function_return` waypoint with `\result == 5` at this call site;
  3. a default.
- **Restarts:** suppose a follow waypoint like `x == 5` or the branching `if (n - x <= 0)` fails, and the value
  depends on an input through its term. The validator then solves for an input value that gives the waypoint its
  outcome without breaking the waypoints the run already passed, fixes it, and restarts the run from the beginning
  (`validate/README.md`, "Search"). Likewise, an avoid waypoint that passes makes the validator change an input so
  that it does not. The search never runs the program to try a value: each restart comes from one prediction. Runs
  are deterministic, so every confirmation comes from one concrete execution, in which every waypoint was evaluated
  on that run's values. The number of restarts grows with the number of waypoints, up to a fixed bound.
- **The verdict** is computed from:
  - whether the error function was called;
  - the monitor's state: target reached, waiting, or rejected;
  - why the run ended: error function, end of program, `exit`, `abort`, a limit, or unsupported input.
  The verdict maps to the exit codes of 3.x (see `docs/exit-codes.md`).

### Errors

- **Problems in the input:** an unsupported or invalid C construct, or a malformed witness. They become diagnostics with
  a source location. Public functions that can fail on input return `std::expected<T, Error>` or report through a
  `DiagnosticEngine`.
- **Bugs in NITWIT:** a violated invariant. It triggers `NITWIT_ASSERT`, which is active in all build types. For a
  validator, aborting is better than a wrong verdict.
- **Exceptions:** allowed only inside a module, to unwind deep recursion (for example the parser giving up, or the
  interpreter stopping a run). They are caught at the module's API boundary.
