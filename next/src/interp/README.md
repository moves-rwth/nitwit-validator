# Module `interp`

Executes the typed AST. Built in steps: first the memory model, later lowering, evaluation and builtins (stage 4).

- `memory.hpp`: `Memory`, the flat virtual address space with an allocation table, initialization tracking and
  origins of nondeterministic values.

## Memory: behaviour to keep

- **Address layout:** deterministic. Each region gets its own part of the address space, and the null page stays
  unmapped. Allocations are separated by gaps of at least 16 bytes. Stack addresses are reused after `popStack`; heap
  addresses are never reused, so use-after-free is always detected.
- **Lazy commit:** a large global array (sv-benchmarks has arrays of hundreds of MB) costs host memory only for the
  pages that are written. `calloc`-backed buffers achieve that on Linux.
- **Origins:** a sparse map from address ranges to input ids. `write` and `fill` drop origins overlapping the range;
  `copy` moves them along with the bytes.
- **Lookup:** finding the allocation of an address is on the hot path of every load and store. Keep the last
  allocation found as a cache.

## Tests

- `tests/unit/interp/memory_test.cpp`: allocation in every region with sizes 0, 1 and large; alignment; null page;
  gaps between allocations; reads and writes inside, across the end, one past the end; dead heap blocks; double free;
  free of non-base and non-heap addresses; LIFO stack reuse; read-only and code protection; initialization tracking per
  byte; origins set, read, overwritten partly, copied with `copy` (overlapping ranges too); determinism of addresses;
  32-bit address space limits; a 1 GiB global that is written at two places only.
