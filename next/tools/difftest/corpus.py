#!/usr/bin/env python3
"""Runs a deterministic sample of the dataset programs with random inputs under GCC, Clang and a sanitizer build.

Each program is compiled for the data model of its witnesses (the `architecture` field of index.jsonl) and run with
several input vectors (inputs.py). Every run is classified as compile-failure, timeout, sanitizer, disagree or agree;
the result is a TSV with one row per program and input vector, plus a summary.
"""

import argparse
import collections
import concurrent.futures
import json
import os
import random
import sys
from dataclasses import dataclass, fields
from pathlib import Path

import difftest
import inputs as inputs_module

DEFAULT_DATASET = Path(os.environ.get("NITWIT_DATASET_DIR", "/mnt/e/workspace/playground/svcomp-2026-witnesses"))

# Categories, most significant first: the category of a program is the most significant one of its runs.
CATEGORIES = ("compile-failure", "timeout", "sanitizer", "disagree", "agree")


@dataclass
class Row:
    """One line of the TSV: a program run with one input vector ("-" if it did not compile)."""

    program: str
    bits: int
    vector: str
    category: str
    outcome: str  # the outcome of the GCC run, if it ran
    detail: str
    gcc: str = "-"
    clang: str = "-"
    sanitizer: str = "-"


def load_programs(dataset: Path, prop: str) -> dict[str, int]:
    """Returns the programs of the witnesses with the property ("all": any) and their data model in bits.

    A program whose witnesses name both data models gets the more frequent one.
    """
    models: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    with open(dataset / "index.jsonl") as index:
        for line in index:
            record = json.loads(line)
            if record.get("program") and (prop == "all" or record.get("property") == prop):
                models[record["program"]][record.get("architecture")] += 1
    programs = {}
    for program, counter in models.items():
        architecture = max(sorted(counter), key=counter.__getitem__)
        programs[program] = 32 if architecture == "32bit" else 64
    return programs


def sample(programs: dict[str, int], size: int, seed: int) -> list[tuple[str, int]]:
    """Returns a deterministic random sample of (program, bits)."""
    names = sorted(programs)
    chosen = random.Random(seed).sample(names, min(size, len(names)))
    return [(name, programs[name]) for name in chosen]


def classify(program: str, bits: int, vector: int, runs: list[difftest.RunResult]) -> Row:
    """Classifies the runs of one input vector; runs are those of GCC, Clang and the sanitizer build."""
    gcc, clang, sanitizer = runs
    outcomes = [run.outcome() for run in runs]
    row = Row(program, bits, str(vector), "agree", outcomes[0], "", *outcomes)
    limited = [f"{run.name}:{run.outcome()}" for run in runs if run.limit_exceeded]
    if limited:
        row.category, row.detail = "timeout", " ".join(limited)
    elif sanitizer.outcome() == "sanitizer":
        row.category, row.detail = "sanitizer", difftest.sanitizer_report(sanitizer.stderr) or ""
    elif difftest.differences(gcc, clang):
        row.category, row.detail = "disagree", "gcc/clang: " + ",".join(difftest.differences(gcc, clang))
    elif difftest.differences(gcc, sanitizer):
        row.category, row.detail = "disagree", "gcc/sanitizer: " + ",".join(difftest.differences(gcc, sanitizer))
    return row


def check_program(dataset: Path, program: str, bits: int, args: argparse.Namespace) -> list[Row]:
    """Compiles one program with every build and runs it with every input vector."""
    limits = difftest.Limits(timeout=args.timeout, memory_mb=args.memory)
    builds = difftest.default_builds(bits, args.sanitizer_compiler)
    with difftest.temporary_directory("difftest-corpus-", args.work_dir) as directory:
        build_dir = Path(directory)
        compiled = [difftest.compile_program(dataset / program, build, build_dir, limits) for build in builds]
        failed = [result for result in compiled if result.binary is None]
        if failed:
            names = ",".join(result.build.name for result in failed)
            return [Row(program, bits, "-", "compile-failure", "-", f"{names}: {failed[0].message}")]
        rows = []
        for vector in range(args.vectors):
            inputs = build_dir / f"inputs-{vector}.txt"
            inputs_module.write(inputs, args.input_seed + vector, args.input_count)
            runs = [difftest.run_binary(result, inputs, limits, args.work_dir) for result in compiled]
            rows.append(classify(program, bits, vector, runs))
        return rows


def write_tsv(path: Path, rows: list[Row]) -> None:
    """Writes the rows as a TSV with a header line."""
    names = [field.name for field in fields(Row)]
    with open(path, "w") as file:
        file.write("\t".join(names) + "\n")
        for row in rows:
            file.write("\t".join(str(getattr(row, name)).replace("\t", " ") for name in names) + "\n")


def read_tsv(path: Path) -> list[Row]:
    """Reads a TSV written by write_tsv."""
    with open(path) as file:
        header = file.readline().rstrip("\n").split("\t")
        rows = []
        for line in file:
            values = dict(zip(header, line.rstrip("\n").split("\t")))
            values["bits"] = int(values["bits"])
            rows.append(Row(**values))
        return rows


def summarize(rows: list[Row]) -> str:
    """Returns counts per category for programs and runs, and the most frequent details."""
    by_program: dict[str, list[Row]] = collections.defaultdict(list)
    for row in rows:
        by_program[row.program].append(row)
    program_category = {
        program: min((row.category for row in program_rows), key=CATEGORIES.index)
        for program, program_rows in by_program.items()
    }
    runs = [row for row in rows if row.vector != "-"]
    lines = [f"{len(by_program)} programs, {len(runs)} runs (program and input vector)", ""]

    lines.append("Programs by category (the most significant category of their runs):")
    counts = collections.Counter(program_category.values())
    for category in CATEGORIES:
        lines.append(f"  {category:16} {counts[category]:5}")
    bits = collections.Counter(rows_[0].bits for rows_ in by_program.values())
    lines.append(f"  (data models: {bits[32]} programs 32-bit, {bits[64]} programs 64-bit)")
    agreeing = [program for program, category in program_category.items() if category == "agree"]
    reaching = sum(any(row.outcome == "error" for row in by_program[program]) for program in agreeing)
    lines.append(f"  agree on all vectors and reach the error function with at least one: {reaching}")

    lines += ["", "Runs by category:"]
    run_counts = collections.Counter(row.category for row in runs)
    for category in CATEGORIES[1:]:
        lines.append(f"  {category:16} {run_counts[category]:5}")
    lines += ["", "Agreeing runs by outcome:"]
    for outcome, count in collections.Counter(row.outcome for row in runs if row.category == "agree").most_common():
        lines.append(f"  {outcome:16} {count:5}")

    def top(title: str, values: list[str], limit: int = 15) -> None:
        lines.extend(["", title])
        for value, count in collections.Counter(values).most_common(limit):
            lines.append(f"  {count:5}  {value}")

    top("Compile failures by reason (programs):", [r.detail for r in rows if r.category == "compile-failure"])
    top("Timeouts by builds (runs):", [" ".join(sorted(b.split(":")[0] for b in r.detail.split())) for r in runs
                                       if r.category == "timeout"])
    top("Sanitizer findings (runs):", [r.detail for r in runs if r.category == "sanitizer"])
    top("Disagreements (runs):", [f"{r.detail}  [{r.gcc} / {r.clang} / {r.sanitizer}]" for r in runs
                                  if r.category == "disagree"])
    return "\n".join(lines) + "\n"


def command_run(args: argparse.Namespace) -> int:
    """Runs the sample and writes the TSV and the summary."""
    programs = load_programs(args.dataset, args.property)
    chosen = sample(programs, args.size, args.seed)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    print(f"{len(chosen)} of {len(programs)} programs, {args.vectors} input vectors, {args.jobs} jobs", file=sys.stderr)

    rows: list[Row] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = pool.map(lambda item: check_program(args.dataset, item[0], item[1], args), chosen)
        for done, program_rows in enumerate(results, start=1):
            rows.extend(program_rows)
            if done % 25 == 0 or done == len(chosen):
                print(f"  {done}/{len(chosen)}", file=sys.stderr)
                write_tsv(args.output, rows)
    summary = summarize(rows)
    args.output.with_suffix(".summary.txt").write_text(summary)
    print(summary, end="")
    return 0


def command_summary(args: argparse.Namespace) -> int:
    """Prints the summary of an existing TSV."""
    print(summarize(read_tsv(args.tsv)), end="")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    run = sub.add_parser("run", help="run a sample of the dataset programs and write a TSV")
    run.add_argument("--dataset", type=Path, default=DEFAULT_DATASET, help=f"dataset directory ({DEFAULT_DATASET})")
    run.add_argument("--property", default="unreach-call", help="witness property of the programs, or 'all'")
    run.add_argument("--size", type=int, default=500, help="number of programs (default 500)")
    run.add_argument("--seed", type=int, default=2027, help="seed of the sample (default 2027)")
    run.add_argument("--vectors", type=int, default=3, help="input vectors per program (default 3)")
    run.add_argument("--input-seed", type=int, default=1, help="seed of the first input vector (default 1)")
    run.add_argument("--input-count", type=int, default=1000, help="values per input vector (default 1000)")
    run.add_argument("--timeout", type=float, default=5.0, help="wall-clock limit per run in seconds (default 5)")
    run.add_argument("--memory", type=int, default=2048, help="memory limit per run in MiB (default 2048)")
    run.add_argument("--sanitizer-compiler", choices=difftest.COMPILERS, default=difftest.CLANG,
                     help="compiler of the sanitizer build")
    run.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2), help="programs in parallel")
    run.add_argument("--work-dir", type=Path, help="directory for temporary files (default: the system's)")
    run.add_argument("-o", "--output", type=Path, default=Path.home() / "difftest-out" / "corpus.tsv",
                     help="output TSV; the summary goes next to it (default ~/difftest-out/corpus.tsv)")
    run.set_defaults(handler=command_run)

    summary = sub.add_parser("summary", help="print the summary of a TSV written by 'run'")
    summary.add_argument("tsv", type=Path)
    summary.set_defaults(handler=command_summary)

    args = parser.parse_args(argv)
    return args.handler(args)


if __name__ == "__main__":
    sys.exit(main())
