#!/usr/bin/env python3
"""Differential testing of C programs: compile them with GCC and Clang, run them with the harness, compare the runs.

Library and command line, see README.md. Programs are untrusted: every run gets resource limits and a fresh temporary
working directory, and its output is captured in files whose size is limited too.
"""

import argparse
import concurrent.futures
import functools
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent
HARNESS = TOOL_DIR / "harness.c"
SEMANTICS_DIR = TOOL_DIR.parent.parent / "tests" / "semantics"

GCC = "gcc-14"
CLANG = "clang-20"
COMPILERS = (GCC, CLANG)

# Exit codes of the harness (README.md, "Harness") and of the sanitizer runtimes (set through ASAN_OPTIONS and
# UBSAN_OPTIONS below).
EXIT_ERROR = 97
EXIT_ASSUME = 98
EXIT_BAD_INPUT = 99
EXIT_SANITIZER = 86
MARKER = b"NITWIT-DIFFTEST: "

# GCC 14 and Clang 16+ reject pre-C99 code (implicit int, implicit function declarations, int/pointer conversions)
# that sv-benchmarks still contains. These flags turn the errors back into warnings, which -w then silences.
PERMISSIVE_FLAGS = {
    GCC: ["-fpermissive"],
    CLANG: [
        "-Wno-error=implicit-function-declaration",
        "-Wno-error=implicit-int",
        "-Wno-error=int-conversion",
        "-Wno-error=incompatible-pointer-types",
        "-Wno-error=incompatible-function-pointer-types",
        "-Wno-error=return-mismatch",
    ],
}


@dataclass(frozen=True)
class Build:
    """One way to compile a program: compiler, data model (32 or 64 bits) and whether sanitizers are enabled."""

    compiler: str
    bits: int
    sanitize: bool = False

    @property
    def name(self) -> str:
        return f"{self.compiler}-m{self.bits}" + ("-san" if self.sanitize else "")

    def flags(self) -> list[str]:
        """Returns the compiler flags, see README.md, "Native runs"."""
        flags = ["-O0", "-w", "-fno-builtin", *PERMISSIVE_FLAGS[self.compiler]]
        # -mfpmath=sse avoids the excess precision of x87 arithmetic for float and double.
        flags += ["-m32", "-msse2", "-mfpmath=sse"] if self.bits == 32 else ["-m64"]
        if self.sanitize:
            flags += ["-fsanitize=undefined,address", "-fno-sanitize-recover=all"]
        return flags


@dataclass
class Limits:
    """Resource limits of one run."""

    timeout: float = 10.0  # wall clock, in seconds
    memory_mb: int = 2048  # address space; resident set size for sanitizer builds
    file_mb: int = 16  # size of every file written, including the captured stdout and stderr
    compile_timeout: float = 120.0

    @property
    def cpu_seconds(self) -> int:
        return int(self.timeout) + 1


@dataclass
class CompileResult:
    """The outcome of compiling a program. `binary` is None if compilation failed; `message` then says why."""

    build: Build
    binary: Path | None
    message: str = ""


@dataclass
class RunResult:
    """The observable behaviour of one run."""

    name: str  # the build, or "nitwit"
    status: int | None  # exit status; 128 + n for signal n; None if the wall-clock timeout hit
    stdout: bytes
    stderr: bytes
    input_log: bytes

    @property
    def limit_exceeded(self) -> bool:
        """Whether the run hit the time, file size or (sanitizer builds only) memory limit."""
        return self.outcome() in ("timeout", "output-limit", "memory-limit")

    def outcome(self) -> str:
        """Classifies the run: timeout, output-limit, memory-limit, sanitizer, error, assume, bad-input, abort,
        signal:<name> or exit:<code>."""
        if self.status is None or self.status == 128 + signal.SIGXCPU:
            return "timeout"
        if self.status == 128 + signal.SIGXFSZ:
            return "output-limit"
        if self.status == EXIT_SANITIZER and b"AddressSanitizer: hard rss limit exhausted" in self.stderr:
            return "memory-limit"
        if self.status == EXIT_SANITIZER and sanitizer_report(self.stderr):
            return "sanitizer"
        if self.status == EXIT_ERROR and MARKER + b"error" in self.stderr:
            return "error"
        if self.status == EXIT_ASSUME and MARKER + b"assume" in self.stderr:
            return "assume"
        if self.status == EXIT_BAD_INPUT and MARKER + b"bad input" in self.stderr:
            return "bad-input"
        if self.status == 128 + signal.SIGABRT:
            return "abort"
        if self.status > 128:
            try:
                return "signal:" + signal.Signals(self.status - 128).name
            except ValueError:
                pass
        return f"exit:{self.status}"


def sanitizer_report(stderr: bytes) -> str | None:
    """Returns a short, groupable description of the first ASan or UBSan report in stderr, or None."""
    text = stderr.decode("utf-8", "replace")
    match = re.search(r"ERROR: (\w+Sanitizer): ([\w-]+)", text)
    if match:
        return f"{match[1]}: {match[2]}"
    match = re.search(r"runtime error: ([^\n]*)", text)
    if match:
        # Replace values and type names, so that equal kinds of reports compare equal.
        message = re.sub(r"'[^']*'", "T", match[1])
        message = re.sub(r"-?\b(0x[0-9a-f]+|\d+)\b", "N", message)
        return "UndefinedBehaviorSanitizer: " + message
    return None


def temporary_directory(prefix: str, root: Path | None = None) -> tempfile.TemporaryDirectory:
    """Returns a temporary directory below root (default: the system's); its removal does not fail on files that a
    program made unremovable."""
    return tempfile.TemporaryDirectory(prefix=prefix, dir=root, ignore_cleanup_errors=True)


@functools.cache
def sanitizer_available(compiler: str, bits: int) -> bool:
    """Whether the compiler can link sanitizer builds for the data model (gcc-14 -m32 needs gcc-14-multilib)."""
    with temporary_directory("difftest-probe-") as directory:
        source = Path(directory) / "probe.c"
        source.write_text("int main(void) { return 0; }\n")
        build = Build(compiler, bits, sanitize=True)
        command = [compiler, *build.flags(), str(source), "-o", str(Path(directory) / "probe")]
        return subprocess.run(command, capture_output=True, timeout=120).returncode == 0


def compile_message(stderr: str) -> str:
    """Returns the first error of a compiler or linker output, without file names and line numbers."""
    lines = stderr.strip().splitlines()
    for line in lines:
        if "undefined reference to" in line:
            return "undefined reference to " + line.split("undefined reference to", 1)[1].strip(" `'‘’")
        match = re.search(r"\b(?:fatal )?error: (.*)", line)
        if match and "ld returned" not in line:
            return match[1].strip()
    for line in lines:
        if line.startswith("/usr/bin/ld: "):
            return "ld: " + line.removeprefix("/usr/bin/ld: ")
    return lines[-1] if lines else "no output"


def compile_program(
    program: Path, build: Build, out_dir: Path, limits: Limits | None = None, extra_flags: list[str] | None = None
) -> CompileResult:
    """Compiles the program together with the harness into out_dir; extra_flags are, e.g., include directories."""
    limits = limits or Limits()
    binary = out_dir / build.name
    flags = [*build.flags(), *(extra_flags or [])]
    command = [build.compiler, *flags, str(program), str(HARNESS), "-o", str(binary), "-lm"]
    try:
        result = subprocess.run(command, cwd=out_dir, capture_output=True, timeout=limits.compile_timeout)
    except subprocess.TimeoutExpired:
        return CompileResult(build, None, "compiler timeout")
    if result.returncode != 0:
        return CompileResult(build, None, compile_message(result.stderr.decode("utf-8", "replace")))
    return CompileResult(build, binary)


def _sanitizer_environment(limits: Limits) -> dict[str, str]:
    # ASan reserves terabytes of shadow memory, so it cannot run under an address-space limit; its RSS limit and
    # maximum allocation size take that role. allocator_may_return_null makes huge mallocs fail as they do natively.
    asan = {
        "detect_leaks": 0,
        "exitcode": EXIT_SANITIZER,
        "allocator_may_return_null": 1,
        "max_allocation_size_mb": limits.memory_mb,
        "hard_rss_limit_mb": limits.memory_mb,
        "handle_abort": 0,
    }
    ubsan = {"exitcode": EXIT_SANITIZER, "halt_on_error": 1, "print_stacktrace": 0}
    return {
        "ASAN_OPTIONS": ":".join(f"{key}={value}" for key, value in asan.items()),
        "UBSAN_OPTIONS": ":".join(f"{key}={value}" for key, value in ubsan.items()),
    }


def run_command(
    name: str,
    command: list[str],
    inputs: Path | None,
    limits: Limits,
    sanitize: bool = False,
    work_root: Path | None = None,
) -> RunResult:
    """Runs a command under the limits in a fresh temporary directory.

    The words "{inputs}" and "{log}" in the command are replaced by the paths of the inputs file (a copy) and the
    input log; NITWIT_INPUTS and NITWIT_INPUT_LOG name them as well, for the harness.
    """
    with temporary_directory("difftest-run-", work_root) as directory:
        run_dir = Path(directory)
        inputs_copy = run_dir / "inputs.txt"
        if inputs is not None:
            shutil.copyfile(inputs, inputs_copy)
        else:
            inputs_copy.write_bytes(b"")
        log_path = run_dir / "input.log"
        command = [str(inputs_copy) if w == "{inputs}" else str(log_path) if w == "{log}" else w for w in command]

        env = {
            "PATH": "/usr/bin:/bin",
            "LC_ALL": "C",
            "HOME": str(run_dir),
            "TMPDIR": str(run_dir),
            "NITWIT_INPUTS": str(inputs_copy),
            "NITWIT_INPUT_LOG": str(log_path),
        }
        # Bash's ulimit counts -f and -v in KiB and -t in seconds; exec keeps the process id, so that the timeout
        # below kills the program and not a shell.
        ulimit = f"ulimit -t {limits.cpu_seconds} -f {limits.file_mb * 1024}"
        if sanitize:
            env.update(_sanitizer_environment(limits))
        else:
            ulimit += f" -v {limits.memory_mb * 1024}"
        wrapped = ["bash", "-c", ulimit + ' && exec "$@"', "difftest", *command]

        stdout_path = run_dir / "stdout"
        stderr_path = run_dir / "stderr"
        with open(stdout_path, "wb") as stdout, open(stderr_path, "wb") as stderr:
            process = subprocess.Popen(
                wrapped,
                cwd=run_dir,
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                start_new_session=True,
            )
            try:
                process.wait(timeout=limits.timeout)
                status = process.returncode if process.returncode >= 0 else 128 - process.returncode
            except subprocess.TimeoutExpired:
                status = None
            finally:
                # The program may have started other processes in its session.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()

        return RunResult(
            name=name,
            status=status,
            stdout=stdout_path.read_bytes(),
            stderr=stderr_path.read_bytes(),
            input_log=log_path.read_bytes() if log_path.exists() else b"",
        )


def run_binary(
    compiled: CompileResult, inputs: Path | None, limits: Limits, work_root: Path | None = None
) -> RunResult:
    """Runs a compiled program with the inputs file (None: no inputs, every value is the default)."""
    assert compiled.binary is not None
    build = compiled.build
    return run_command(build.name, [str(compiled.binary)], inputs, limits, build.sanitize, work_root)


def run_interpreter(
    nitwit: Path, program: Path, bits: int, inputs: Path | None, limits: Limits, work_root: Path | None = None
) -> RunResult:
    """Runs the program in the interpreter in replay mode (README.md, "Contract with the interpreter")."""
    command = [str(nitwit), "--replay", "{inputs}", "--input-log", "{log}", f"-{bits}", str(program.resolve())]
    return run_command("nitwit", command, inputs, limits, work_root=work_root)


def differences(a: RunResult, b: RunResult) -> list[str]:
    """Returns what differs between two runs (README.md, "Comparison"); a run that hit a limit agrees with nothing."""
    if a.limit_exceeded or b.limit_exceeded:
        return ["limit"]
    found = []
    if a.status != b.status:
        found.append("status")
    if a.stdout != b.stdout:
        found.append("stdout")
    if a.input_log != b.input_log:
        found.append("input-log")
    return found


def agree(a: RunResult, b: RunResult) -> bool:
    """Whether two runs agree."""
    return not differences(a, b)


def default_builds(bits: int, sanitizer_compiler: str | None = CLANG) -> list[Build]:
    """The plain builds with both compilers, plus a sanitizer build unless sanitizer_compiler is None."""
    builds = [Build(compiler, bits) for compiler in COMPILERS]
    if sanitizer_compiler is not None:
        builds.append(Build(sanitizer_compiler, bits, sanitize=True))
    return builds


# ---------------------------------------------------------------------------------------------------------------------
# Command `run`


def command_run(args: argparse.Namespace) -> int:
    """Compiles and runs one program with every build and reports whether the runs agree."""
    limits = Limits(timeout=args.timeout, memory_mb=args.memory)
    builds = default_builds(args.bits, None if args.no_sanitizer else args.sanitizer_compiler)
    runs: list[RunResult] = []
    with temporary_directory("difftest-build-", args.work_dir) as build_dir:
        for build in builds:
            compiled = compile_program(args.program, build, Path(build_dir), limits)
            if compiled.binary is None:
                print(f"{build.name:24} compile failure: {compiled.message}")
                return 2
            runs.append(run_binary(compiled, args.inputs, limits, args.work_dir))
    if args.nitwit:
        runs.append(run_interpreter(args.nitwit, args.program, args.bits, args.inputs, limits, args.work_dir))

    reference = runs[0]
    findings = [run for run in runs if run.outcome() == "sanitizer"]
    limited = [run for run in runs if run.limit_exceeded]
    disagreeing = [run for run in runs[1:] if run not in findings + limited and differences(reference, run)]
    for run in runs:
        inputs_used = run.input_log.count(b"\n")
        line = f"{run.name:24} {run.outcome():16} stdout {len(run.stdout):7} B, {inputs_used:5} inputs"
        if run in findings:
            line += "   " + (sanitizer_report(run.stderr) or "")
        elif run in disagreeing:
            line += f"   differs from {reference.name}: {', '.join(differences(reference, run))}"
        print(line)
        if args.verbose:
            for label, data in (("stdout", run.stdout), ("stderr", run.stderr), ("input log", run.input_log)):
                if data:
                    print(f"  --- {label}")
                    print("  " + data.decode("utf-8", "replace").rstrip("\n").replace("\n", "\n  "))
    if findings:
        print("sanitizer finding")
    elif limited:
        print("limit exceeded")
    elif disagreeing:
        print("DISAGREE")
    else:
        print("agree")
    return 0 if not (findings or limited or disagreeing) else 1


# ---------------------------------------------------------------------------------------------------------------------
# Command `semantics`


@dataclass
class SemanticsResult:
    """The result of one semantics test for one data model."""

    name: str
    bits: int
    problems: list[str]
    runs: list[RunResult]


def semantics_builds(bits: int) -> list[Build]:
    """Both compilers, plain and with sanitizers where the sanitizer runtime is installed."""
    builds = [Build(compiler, bits) for compiler in COMPILERS]
    builds += [Build(compiler, bits, sanitize=True) for compiler in COMPILERS if sanitizer_available(compiler, bits)]
    return builds


def check_semantics_test(
    program: Path, bits: int, limits: Limits, nitwit: Path | None, update: bool, work_dir: Path | None
) -> SemanticsResult:
    """Runs one semantics test with every build. With update, writes the expected output if all builds agree."""
    name = program.stem
    inputs = program.with_suffix(".inputs")
    inputs_or_none = inputs if inputs.exists() else None
    expected_path = program.with_name(f"{name}.expected-{bits}")
    problems = []
    runs = []
    with temporary_directory("difftest-build-", work_dir) as build_dir:
        for build in semantics_builds(bits):
            compiled = compile_program(program, build, Path(build_dir), limits)
            if compiled.binary is None:
                problems.append(f"{build.name}: compile failure: {compiled.message}")
                continue
            runs.append(run_binary(compiled, inputs_or_none, limits, work_dir))
    if nitwit is not None:
        runs.append(run_interpreter(nitwit, program, bits, inputs_or_none, limits, work_dir))

    for run in runs:
        if run.outcome() == "sanitizer":
            problems.append(f"{run.name}: {sanitizer_report(run.stderr)}")
        elif run.outcome() != "exit:0":
            problems.append(f"{run.name}: {run.outcome()}")
        elif run.stderr:
            problems.append(f"{run.name}: output on stderr: {run.stderr[:200]!r}")

    if update:
        outputs = {run.stdout for run in runs}
        if len(outputs) > 1:
            problems.append("the builds print different outputs: " + ", ".join(run.name for run in runs))
        elif not problems and runs:
            expected_path.write_bytes(runs[0].stdout)
    elif not expected_path.exists():
        problems.append(f"{expected_path.name} is missing (use --update)")
    else:
        expected = expected_path.read_bytes()
        for run in runs:
            if run.stdout != expected:
                problems.append(f"{run.name}: stdout differs from {expected_path.name}")
    return SemanticsResult(name, bits, problems, runs)


def command_semantics(args: argparse.Namespace) -> int:
    """Checks that every build prints exactly the expected output of every semantics test."""
    limits = Limits(timeout=args.timeout)
    programs = sorted(args.directory.glob("*.c"))
    if args.names:
        programs = [p for p in programs if p.stem in args.names]
        missing = set(args.names) - {p.stem for p in programs}
        if missing:
            print("no such test: " + ", ".join(sorted(missing)), file=sys.stderr)
            return 2
    for bits in args.bits:
        semantics_builds(bits)  # probes the sanitizer runtimes once, before the threads start

    jobs = [(program, bits) for program in programs for bits in args.bits]
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [
            pool.submit(check_semantics_test, program, bits, limits, args.nitwit, args.update, args.work_dir)
            for program, bits in jobs
        ]
        for future in futures:
            result = future.result()
            status = "ok" if not result.problems else "FAIL"
            print(f"{status:4} {result.name} -m{result.bits} ({len(result.runs)} runs)")
            for problem in result.problems:
                print(f"     {problem}")
            failed += bool(result.problems)
    print(f"{len(jobs) - failed} of {len(jobs)} passed" + (" (expected files updated)" if args.update else ""))
    return 0 if failed == 0 else 1


# ---------------------------------------------------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    def add_common(p: argparse.ArgumentParser, timeout: float) -> None:
        p.add_argument("--timeout", type=float, default=timeout, help="wall-clock limit per run in seconds")
        p.add_argument("--nitwit", type=Path, help="also run this interpreter binary in replay mode")
        p.add_argument("--work-dir", type=Path, help="directory for temporary files (default: the system's)")

    run = sub.add_parser("run", help="compile and run one program with GCC, Clang and a sanitizer build")
    run.add_argument("program", type=Path, help="the C program (.c or .i)")
    run.add_argument("--bits", type=int, choices=(32, 64), default=64, help="data model (default 64)")
    run.add_argument("--inputs", type=Path, help="inputs file, one value per line (default: no inputs)")
    run.add_argument("--memory", type=int, default=2048, help="memory limit in MiB")
    run.add_argument("--sanitizer-compiler", choices=COMPILERS, default=CLANG, help="compiler of the sanitizer build")
    run.add_argument("--no-sanitizer", action="store_true", help="skip the sanitizer build")
    run.add_argument("-v", "--verbose", action="store_true", help="print stdout, stderr and the input log of each run")
    add_common(run, 10.0)
    run.set_defaults(handler=command_run)

    semantics = sub.add_parser("semantics", help="check tests/semantics against GCC and Clang")
    semantics.add_argument("names", nargs="*", help="tests to run (default: all)")
    semantics.add_argument("--update", action="store_true", help="write <name>.expected-<bits> if all builds agree")
    semantics.add_argument("--bits", type=int, nargs="+", choices=(32, 64), default=[32, 64], help="data models")
    semantics.add_argument("--directory", type=Path, default=SEMANTICS_DIR, help="test directory")
    semantics.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1), help="parallel tests (default 8)")
    add_common(semantics, 10.0)
    semantics.set_defaults(handler=command_semantics)

    args = parser.parse_args(argv)
    return args.handler(args)


if __name__ == "__main__":
    sys.exit(main())
