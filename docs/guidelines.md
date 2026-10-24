# NITWIT 4: coding guidelines

The guiding question for every change: will someone who knows C++ and C, but not this code base, understand this in a
year?

## Language

C++23 as far as GCC 14 and Clang 20 with libstdc++ 14 support it.

- **Use:** `std::expected`, `std::format`, `std::print`, `std::span`, `std::string_view`, ranges and views including
  `std::ranges::to`, concepts, designated initializers, `enum class`, `std::to_underlying`, `std::unreachable`,
  `constexpr`/`consteval`, `[[nodiscard]]`, structured bindings, `std::optional`, `std::variant` (sparingly), deducing
  `this` (where it removes real duplication).
- **Not yet:** `std::flat_map` (GCC 15), modules, `import std`. `std::generator` only where a plain loop or a callback
  would be harder to read.
- `__int128` is used through `support/int128.hpp` only.
- No compiler-specific code outside `support/`. The code must compile without warnings under
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` and the other warnings in `cmake/Warnings.cmake`.

## Naming and layout

- Files: `snake_case.hpp` and `snake_case.cpp`. The public headers of a module are in `src/<module>/` and are included
  as `#include "<module>/<file>.hpp"`.
- Types, enumerators and concepts: `PascalCase`. Functions, variables and parameters: `camelCase`. Private data
  members: `camelCase_`. Namespaces: `nitwit::<module>`.
- Constants: `constexpr` variables in `camelCase`. Avoid macros; the only macros are those in `support/assert.hpp`.
- `#pragma once`. Include what you use. In headers, prefer forward declarations to includes.
- `clang-format-20` (`.clang-format` at the root of the repository) is the authority on formatting. Run it on every
  changed file.

## Design

- **Small, concrete APIs.** A class with one implementation needs no interface. Use virtual functions where the
  design calls for several implementations (observer, oracle, monitor).
- **Ownership:**
  - AST nodes and types belong to the `AstContext` arena and are passed as raw pointers, `const` wherever possible.
  - Anything else owned is a value or a `std::unique_ptr`.
  - No `new`/`delete` outside arenas. No `shared_ptr` unless ownership is actually shared.
- **No global mutable state.** State lives in objects that are passed explicitly. This is what makes restarts and
  tests simple.
- **Errors:** see "Errors" in `docs/architecture.md`.
  - Problems in the input are diagnostics or `std::expected`.
  - Bugs are `NITWIT_ASSERT(condition, "message")`.
  - Exceptions do not cross module boundaries.
- **Determinism:** no dependence on addresses of host objects, hash iteration order, or time. Two runs on the same
  input must produce the same output.
- **Performance:** choose sensible data structures from the start (vectors, arenas, interned strings). Optimize only
  with a measurement in hand, and never at the cost of clarity without a comment saying why.

## Comments

- **A comment says what the code cannot.** That covers intent, invariants, the reason for a non-obvious choice, and the
  rule of the C standard or the witness format being implemented, cited like `C17 6.5.7p4` or
  `witness format 2.1, "evaluation of waypoints"`.
- **Every public header starts with one or two sentences** on what the module part is for.
- **Document public functions in one line.** Add more only for preconditions, ownership or error behaviour.
- **No comments that restate the code.** No commented-out code. No change logs in comments.
- **No `TODO`** without a reference to an item of `docs/roadmap.md`, e.g. `// TODO(roadmap, GraphML): ...`.
- **Write full English sentences, and keep them short.**

## Tests

- Every module has unit tests in `tests/unit/<module>/`, using GoogleTest.
- Every bug fix comes with a test that fails without the fix.
- Tests are deterministic, independent of each other, and fast (a unit test well under a second).
- Each test checks one behaviour, and its name says which one (`LexerTest, LineMarkerSetsPresumedLine`).
- Test data lives next to the tests (`tests/unit/<module>/data/`), or in `tests/golden/` and `tests/semantics/`.
- Before a change is merged, it builds and passes the tests with both compilers and with the `asan` preset
  (`tools/check.sh`).

## Commits

- One logical change per commit. The message is a sentence in the imperative that ends with a period, like the
  existing history, plus a body where the change needs explanation.
- No AI attribution or `Co-Authored-By` lines.

## Review checklist

Every change is checked against this list before it is merged.

1. **Correctness:** C or witness-format semantics as specified (with citations); edge cases (empty input, maximum
   values, 32 vs. 64 bit); error paths.
2. **API adherence:** the public header is implemented as documented; nothing else became public.
3. **Simplicity:** no speculative generality, no duplicated logic, no dead code; functions short enough to grasp.
4. **Comments:** concise, where they add value, accurate.
5. **Tests:** every behaviour covered, including failure cases; tests readable.
6. **Robustness:** no undefined behaviour, no unchecked input, bounded recursion where input can be deep.
7. **Consistency:** naming, formatting and idioms of the surrounding code.
