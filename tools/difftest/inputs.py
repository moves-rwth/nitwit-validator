#!/usr/bin/env python3
"""Generates an inputs file for the harness from a seed: one decimal integer per line.

The order and the types of a program's nondet calls are unknown, so every line is a decimal integer, which the harness
accepts for every __VERIFIER_nondet_* function, the floating-point ones included. The values mix small numbers (loop
bounds, flags), values at the edges of the integer types, and random 8-, 16-, 32- and 64-bit patterns.
"""

import argparse
import os
import random
import sys

# Values at the edges of the C integer types, and their neighbours.
BOUNDARY_VALUES = [
    0, 1, -1, 2, -2,
    127, 128, -128, -129, 255, 256,
    32767, 32768, -32768, -32769, 65535, 65536,
    2147483647, 2147483648, -2147483648, -2147483649, 4294967295, 4294967296,
    9223372036854775807, -9223372036854775808, 18446744073709551615,
]  # fmt: skip


def random_value(rng: random.Random) -> int:
    """Returns one input value."""
    kind = rng.random()
    if kind < 0.4:
        return rng.randint(-4, 16)
    if kind < 0.6:
        return rng.choice(BOUNDARY_VALUES)
    if kind < 0.75:
        return rng.randint(-128, 255)
    if kind < 0.85:
        return rng.randint(-32768, 65535)
    if kind < 0.95:
        return rng.randint(-(2**31), 2**32 - 1)
    return rng.randint(-(2**63), 2**64 - 1)


def generate(seed: int, count: int) -> list[int]:
    """Returns count input values; equal seeds give equal values."""
    rng = random.Random(seed)
    return [random_value(rng) for _ in range(count)]


def write(path: str | os.PathLike, seed: int, count: int) -> None:
    """Writes an inputs file."""
    with open(path, "w") as file:
        file.writelines(f"{value}\n" for value in generate(seed, count))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("seed", type=int, help="random seed")
    parser.add_argument("--count", type=int, default=1000, help="number of lines (default 1000)")
    parser.add_argument("-o", "--output", help="output file (default: stdout)")
    args = parser.parse_args(argv)
    if args.output:
        write(args.output, args.seed, args.count)
    else:
        sys.stdout.writelines(f"{value}\n" for value in generate(args.seed, args.count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
