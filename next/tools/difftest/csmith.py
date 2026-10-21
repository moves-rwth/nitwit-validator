#!/usr/bin/env python3
"""Generates Csmith programs and preprocesses them to .i files for one data model.

Needs csmith 2.3 and its headers (Ubuntu: `sudo apt install csmith libcsmith-dev`). Csmith programs read no inputs;
they print a checksum of all globals, which makes them a strong check on integer promotions, layouts and pointer
arithmetic.

For every seed, the output directory gets csmith-<seed>.c and csmith-<seed>-m<bits>.i, preprocessed by GCC (the
system cpp the interpreter will use). With --check, the .c file is compiled and run with GCC, Clang and a sanitizer
build, and the runs are compared. The native builds compile the .c file, not the .i file, because glibc's headers
expand differently for GCC and Clang (e.g. _Float128 and __malloc__ attributes with arguments, which Clang rejects).
"""

import argparse
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys
from pathlib import Path

import difftest


class CsmithMissing(Exception):
    """csmith or its headers are not installed."""


def find_csmith() -> Path:
    """Returns the csmith executable: $CSMITH, or csmith on the PATH."""
    path = os.environ.get("CSMITH") or shutil.which("csmith")
    if not path:
        raise CsmithMissing("csmith is not installed (Ubuntu: sudo apt install csmith), or set CSMITH to its path")
    return Path(path)


def find_include_dir(csmith: Path) -> Path:
    """Returns the directory that contains csmith.h: $CSMITH_INCLUDE, or a csmith include directory of the
    installation prefix, /usr or /usr/local (Ubuntu's libcsmith-dev: /usr/include/csmith; csmith built from source:
    <prefix>/include/csmith-2.3.0)."""
    candidates = [os.environ.get("CSMITH_INCLUDE", "")]
    for prefix in (csmith.resolve().parent.parent, Path("/usr"), Path("/usr/local")):
        candidates += [str(prefix / "include"), *sorted(glob.glob(str(prefix / "include" / "csmith*")), reverse=True)]
    for candidate in candidates:
        if candidate and (Path(candidate) / "csmith.h").exists():
            return Path(candidate)
    raise CsmithMissing(
        "csmith.h not found (Ubuntu: sudo apt install libcsmith-dev), or set CSMITH_INCLUDE to its directory"
    )


def generate(seed: int, bits: int, out_dir: Path, include: Path, csmith_args: list[str]) -> Path:
    """Generates the program for the seed into out_dir and preprocesses it for the data model; returns the .c file."""
    source = out_dir / f"csmith-{seed}.c"
    # csmith writes platform.info into its working directory.
    command = [str(find_csmith()), "--seed", str(seed), *csmith_args]
    result = subprocess.run(command, cwd=out_dir, capture_output=True, timeout=120)
    if result.returncode != 0:
        raise RuntimeError(f"csmith failed for seed {seed}: {result.stderr.decode(errors='replace').strip()}")
    source.write_bytes(result.stdout)
    preprocessed = out_dir / f"csmith-{seed}-m{bits}.i"
    command = [difftest.GCC, f"-m{bits}", "-E", f"-I{include}", str(source), "-o", str(preprocessed)]
    subprocess.run(command, check=True, capture_output=True, timeout=120)
    return source


def check(source: Path, include: Path, bits: int, limits: difftest.Limits) -> str:
    """Compiles and runs a generated program with every build; returns the classification of the runs."""
    with difftest.temporary_directory("difftest-csmith-") as directory:
        runs = []
        for build in difftest.default_builds(bits):
            compiled = difftest.compile_program(source, build, Path(directory), limits, [f"-I{include}"])
            if compiled.binary is None:
                return f"compile-failure {build.name}: {compiled.message}"
            runs.append(difftest.run_binary(compiled, None, limits))
    if any(run.limit_exceeded for run in runs):
        return "timeout " + " ".join(f"{run.name}:{run.outcome()}" for run in runs)
    if runs[-1].outcome() == "sanitizer":
        return "sanitizer " + (difftest.sanitizer_report(runs[-1].stderr) or "")
    diffs = [
        f"{run.name}:{','.join(difftest.differences(runs[0], run))}"
        for run in runs[1:]
        if difftest.differences(runs[0], run)
    ]
    if diffs:
        return "disagree " + " ".join(diffs)
    return f"agree {runs[0].outcome()} {runs[0].stdout.decode(errors='replace').strip()}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seed", type=int, default=1, help="seed of the first program (default 1)")
    parser.add_argument("--count", type=int, default=1, help="number of programs, with consecutive seeds")
    parser.add_argument("--bits", type=int, choices=(32, 64), default=64, help="data model (default 64)")
    parser.add_argument("--out", type=Path, required=True, help="output directory (outside the repository)")
    parser.add_argument("--check", action="store_true", help="compile, run and compare every program")
    parser.add_argument("--timeout", type=float, default=10.0, help="wall-clock limit per run with --check")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="programs in parallel")
    parser.add_argument("csmith_args", nargs="*", help="further csmith options, after --")
    args = parser.parse_args(argv)

    try:
        include = find_include_dir(find_csmith())
    except CsmithMissing as error:
        print(f"csmith.py: {error}", file=sys.stderr)
        return 2
    args.out.mkdir(parents=True, exist_ok=True)
    limits = difftest.Limits(timeout=args.timeout)

    def one(seed: int) -> str:
        source = generate(seed, args.bits, args.out, include, args.csmith_args)
        return f"{source}\t{check(source, include, args.bits, limits)}" if args.check else str(source)

    seeds = range(args.seed, args.seed + args.count)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for line in pool.map(one, seeds):
            print(line, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
