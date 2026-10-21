#!/usr/bin/env python3
"""Differential testing of C programs: compile them with GCC and Clang, run them with the harness, compare the runs.

Library and command line, see README.md. Programs are untrusted: every compile and every run happens in a systemd scope
with a memory and task limit (where systemd-run is available), under ulimit limits, in its own session and a fresh
temporary directory, and everything it started is killed when it ends.
"""

import argparse
import concurrent.futures
import functools
import os
import re
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import uuid
from dataclasses import dataclass
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent
HARNESS = TOOL_DIR / "harness.c"
SEMANTICS_DIR = TOOL_DIR.parent.parent / "tests" / "semantics"

GCC = "gcc-14"
CLANG = "clang-20"
COMPILERS = (GCC, CLANG)

# Exit codes of the harness (README.md, "Harness"), of the sanitizer runtimes (set through ASAN_OPTIONS and
# UBSAN_OPTIONS below), and of the wrapper script when it cannot set the resource limits.
EXIT_ERROR = 97
EXIT_ASSUME = 98
EXIT_BAD_INPUT = 99
EXIT_SANITIZER = 86
EXIT_SETUP = 125
MARKER = b"NITWIT-DIFFTEST: "
SETUP_FAILURE = b"difftest: cannot set the resource limits"

COMMON_FLAGS = ["-O0", "-w", "-fno-builtin", "-fno-stack-protector"]

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

# A pattern build fills uninitialized automatic variables with a pattern, and glibc fills fresh heap blocks with
# 165 ^ 0xff, so that a program that reads uninitialized memory disagrees with the other plain build.
PATTERN_FLAGS = ["-ftrivial-auto-var-init=pattern"]
MALLOC_PERTURB = "165"

# Outcomes of runs that hit a resource limit; such a run agrees with nothing.
LIMIT_OUTCOMES = ("timeout", "killed", "output-limit", "memory-limit", "stack-overflow")


@dataclass(frozen=True)
class Build:
    """One way to compile a program: compiler, data model (32 or 64 bits), sanitizers, pattern initialization."""

    compiler: str
    bits: int
    sanitize: bool = False
    pattern: bool = False

    @property
    def name(self) -> str:
        """The build's name in reports, e.g. clang-20-m32-san."""
        suffix =("-san" if self.sanitize else "") + ("-pattern" if self.pattern else "")
        return f"{self.compiler}-m{self.bits}{suffix}"

    def flags(self) -> list[str]:
        """Returns the compiler flags, see README.md, "Native runs"."""
        flags = [*COMMON_FLAGS, *PERMISSIVE_FLAGS[self.compiler]]
        # -mfpmath=sse avoids the excess precision of x87 arithmetic for float and double.
        flags += ["-m32", "-msse2", "-mfpmath=sse"] if self.bits == 32 else ["-m64"]
        if self.sanitize:
            flags += ["-fsanitize=undefined,address", "-fno-sanitize-recover=all"]
        if self.pattern:
            flags += PATTERN_FLAGS
        return flags

    def environment(self, limits: "Limits") -> dict[str, str]:
        """Returns the environment variables that runs of this build need besides the common ones."""
        environment = sanitizer_environment(limits) if self.sanitize else {}
        if self.pattern:
            environment["MALLOC_PERTURB_"] = MALLOC_PERTURB
        return environment


@dataclass
class Limits:
    """Resource limits of one run or compile."""

    timeout: float = 10.0  # wall clock, in seconds
    memory_mb: int = 2048  # memory cgroup and address space; for sanitizer builds, cgroup and resident set size
    file_mb: int = 16  # size of every file written, including the captured stdout and stderr
    compile_timeout: float = 120.0

    @property
    def cpu_seconds(self) -> int:
        """The soft CPU time limit; the wall-clock timeout normally ends a run first."""
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
    status: int | None  # exit status, or -n for signal n; None if the wall-clock timeout hit
    stdout: bytes
    stderr: bytes
    input_log: bytes

    @property
    def limit_exceeded(self) -> bool:
        """Whether the run hit a time, memory, output or stack limit."""
        return self.outcome() in LIMIT_OUTCOMES

    def outcome(self) -> str:
        """Classifies the run: one of LIMIT_OUTCOMES, sanitizer, error, assume, bad-input, abort, signal:<name> or
        exit:<code>."""
        if self.status is None:
            return "timeout"
        if self.status < 0:
            number = -self.status
            if number == signal.SIGXCPU:
                return "timeout"
            if number == signal.SIGKILL:
                return "killed"  # the hard CPU limit or the memory cgroup
            if number == signal.SIGXFSZ:
                return "output-limit"
            if number == signal.SIGABRT:
                return "abort"
            try:
                return "signal:" + signal.Signals(number).name
            except ValueError:
                return f"signal:{number}"
        if self.status == EXIT_SANITIZER:
            if any(text in self.stderr for text in (b"hard rss limit exhausted", b"Sanitizer: out of memory")):
                return "memory-limit"
            report = sanitizer_report(self.stderr)
            if report == "AddressSanitizer: stack-overflow":
                return "stack-overflow"
            if report:
                return "sanitizer"
        if self.status == EXIT_ERROR and MARKER + b"error" in self.stderr:
            return "error"
        if self.status == EXIT_ASSUME and MARKER + b"assume" in self.stderr:
            return "assume"
        if self.status == EXIT_BAD_INPUT and MARKER + b"bad input" in self.stderr:
            return "bad-input"
        return f"exit:{self.status}"


def markers(stderr: bytes) -> list[bytes]:
    """Returns the harness's marker lines in stderr."""
    return [line for line in stderr.splitlines() if line.startswith(MARKER)]


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


def sanitizer_environment(limits: Limits) -> dict[str, str]:
    """Returns ASAN_OPTIONS and UBSAN_OPTIONS for sanitizer runs.

    ASan reserves terabytes of shadow memory, so it cannot run under an address-space limit; its RSS limit and maximum
    allocation size take that role. allocator_may_return_null makes huge mallocs fail as they do natively.
    """
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


def temporary_directory(prefix: str, root: Path | None = None) -> tempfile.TemporaryDirectory:
    """Returns a temporary directory below root (default: the system's); its removal does not fail on files that a
    program made unremovable."""
    return tempfile.TemporaryDirectory(prefix=prefix, dir=root, ignore_cleanup_errors=True)


@functools.cache
def warn(message: str) -> None:
    """Prints a warning once per process."""
    print(f"difftest: warning: {message}", file=sys.stderr)


# ---------------------------------------------------------------------------------------------------------------------
# Running untrusted processes


@functools.cache
def systemd_scope_available() -> bool:
    """Whether transient systemd scopes with a memory limit can be created (systemd-run --user --scope)."""
    command = ["systemd-run", "--user", "--scope", "--quiet", "-p", "MemoryMax=64M", "true"]
    try:
        available = subprocess.run(command, capture_output=True, timeout=30).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        available = False
    if not available:
        warn("systemd-run --user --scope is unavailable: runs are limited by ulimit only, and processes that leave the "
             "session of a run survive it")
    return available


def run_contained(command: list[str], cwd: Path, memory_mb: int, timeout: float, stdout, stderr) -> int | None:
    """Runs a command in its own session and, where available, in a systemd scope with a memory and task limit, and
    kills everything it started when it ends or times out. Returns the exit status (-n for signal n), or None if the
    timeout hit."""
    unit = None
    if systemd_scope_available():
        unit = f"difftest-{uuid.uuid4().hex}"
        properties = [f"MemoryMax={memory_mb}M", "MemorySwapMax=0", "TasksMax=64", "KillSignal=SIGKILL",
                      "CollectMode=inactive-or-failed"]
        # systemd-run --scope executes the command itself, so the process keeps its pid and its exit status.
        command = ["systemd-run", "--user", "--scope", "--quiet", f"--unit={unit}",
                   *(word for p in properties for word in ("-p", p)), "--", *command]
    process = subprocess.Popen(
        command, cwd=cwd, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr, start_new_session=True
    )
    try:
        status = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        status = None
    finally:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        if unit is not None:
            # Stopping the scope also kills processes that left the session with setsid().
            subprocess.run(["systemctl", "--user", "stop", f"{unit}.scope"], capture_output=True, timeout=60)
    return status


def clamped_limit(resource_id: int, value: int, unit: int) -> int:
    """Returns value (in units of unit bytes or seconds), lowered to the hard limit this process inherited."""
    hard = resource.getrlimit(resource_id)[1]
    if hard != resource.RLIM_INFINITY and value * unit > hard:
        warn(f"an inherited hard limit (resource {resource_id}: {hard}) lowers the limit of the runs")
        return hard // unit
    return value


def limit_wrapper(
    argv: list[str],
    cpu_seconds: int | None = None,
    file_kb: int | None = None,
    memory_kb: int | None = None,
    argv0: str | None = None,
) -> list[str]:
    """Returns argv wrapped in bash, which sets the resource limits and then executes it, with argv0 as argv[0].

    The soft CPU limit sends SIGXCPU; the hard limit, one second later, SIGKILL. Bash's ulimit counts -f and -v in KiB.
    """
    settings = []
    if cpu_seconds is not None:
        soft = clamped_limit(resource.RLIMIT_CPU, cpu_seconds, 1)
        hard = clamped_limit(resource.RLIMIT_CPU, cpu_seconds + 1, 1)
        settings += [f"ulimit -S -t {soft}", f"ulimit -H -t {hard}"]
    if file_kb is not None:
        settings.append(f"ulimit -f {clamped_limit(resource.RLIMIT_FSIZE, file_kb, 1024)}")
    if memory_kb is not None:
        settings.append(f"ulimit -v {clamped_limit(resource.RLIMIT_AS, memory_kb, 1024)}")
    failure = f'{{ echo "{SETUP_FAILURE.decode()}" >&2; exit {EXIT_SETUP}; }}'
    execute = f"exec -a {argv0} " if argv0 else "exec "
    script = (" && ".join(settings) + f" || {failure}; " if settings else "") + execute + '"$@"'
    return ["bash", "-c", script, "difftest", *argv]


# ---------------------------------------------------------------------------------------------------------------------
# Compiling and running


@functools.cache
def sanitizer_available(compiler: str, bits: int) -> bool:
    """Whether the compiler can link sanitizer builds for the data model (gcc-14 -m32 needs gcc-14-multilib)."""
    with temporary_directory("difftest-probe-") as directory:
        source = Path(directory) / "probe.c"
        source.write_text("int main(void) { return 0; }\n")
        build = Build(compiler, bits, sanitize=True)
        command = [compiler, *build.flags(), str(source), "-o", str(Path(directory) / "probe")]
        try:
            return subprocess.run(command, capture_output=True, timeout=120).returncode == 0
        except subprocess.TimeoutExpired:
            return False


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
    command = [build.compiler, *build.flags(), *(extra_flags or []), str(program), str(HARNESS), "-o", str(binary)]
    command = limit_wrapper([*command, "-lm"], memory_kb=limits.memory_mb * 1024)
    output_path = out_dir / f"{build.name}.compiler-output"
    with open(output_path, "wb") as output:
        status = run_contained(command, out_dir, limits.memory_mb, limits.compile_timeout, output, subprocess.STDOUT)
    output_text = output_path.read_text(errors="replace")
    if status is None:
        return CompileResult(build, None, "compiler timeout")
    if status < 0:
        return CompileResult(build, None, f"compiler killed by signal {-status} (memory limit?)")
    if status != 0:
        return CompileResult(build, None, compile_message(output_text))
    return CompileResult(build, binary)


def creates_threads(binary: Path) -> bool:
    """Whether the program calls pthread_create or thrd_create. Such programs are nondeterministic natively and not
    supported by the interpreter, so their runs are not compared."""
    result = subprocess.run(["nm", "--undefined-only", str(binary)], capture_output=True, text=True, timeout=60)
    return re.search(r"\b(pthread_create|thrd_create)\b", result.stdout) is not None


def run_command(
    name: str,
    argv: list[str],
    inputs: Path | None,
    limits: Limits,
    environment: dict[str, str] | None = None,
    address_space_limit: bool = True,
    argv0: str | None = None,
    work_root: Path | None = None,
) -> RunResult:
    """Runs a command under the limits, with an empty working directory and a fixed environment.

    The words "{inputs}" and "{log}" in argv are replaced by the paths of the inputs file (a copy) and the input log;
    NITWIT_INPUTS and NITWIT_INPUT_LOG name them as well, for the harness. These files and the captured stdout and
    stderr are in a sibling directory of the working directory.
    """
    with temporary_directory("difftest-run-", work_root) as directory:
        cwd = Path(directory) / "cwd"
        io = Path(directory) / "io"
        cwd.mkdir()
        io.mkdir()
        inputs_copy = io / "inputs"
        if inputs is not None:
            shutil.copyfile(inputs, inputs_copy)
        else:
            inputs_copy.write_bytes(b"")
        log_path = io / "input.log"
        argv = [str(inputs_copy) if w == "{inputs}" else str(log_path) if w == "{log}" else w for w in argv]

        env = {
            "PATH": "/usr/bin:/bin",
            "LC_ALL": "C",
            "HOME": "/nonexistent",
            "TMPDIR": "/tmp",
            "NITWIT_INPUTS": str(inputs_copy),
            "NITWIT_INPUT_LOG": str(log_path),
            **(environment or {}),
        }
        memory_kb = limits.memory_mb * 1024 if address_space_limit else None
        wrapped = limit_wrapper(argv, limits.cpu_seconds, limits.file_mb * 1024, memory_kb, argv0)
        command = ["env", "-i", *(f"{key}={value}" for key, value in env.items()), *wrapped]
        with open(io / "stdout", "wb") as stdout, open(io / "stderr", "wb") as stderr:
            status = run_contained(command, cwd, limits.memory_mb, limits.timeout, stdout, stderr)

        result = RunResult(
            name=name,
            status=status,
            stdout=(io / "stdout").read_bytes(),
            stderr=(io / "stderr").read_bytes(),
            input_log=log_path.read_bytes() if log_path.exists() else b"",
        )
    if status == EXIT_SETUP and SETUP_FAILURE in result.stderr:
        raise RuntimeError(f"{name}: {result.stderr.decode(errors='replace').strip()}")
    return result


def run_binary(
    compiled: CompileResult, inputs: Path | None, limits: Limits, work_root: Path | None = None
) -> RunResult:
    """Runs a compiled program with the inputs file (None: no inputs, every value is the default)."""
    assert compiled.binary is not None
    build = compiled.build
    if build.sanitize and resource.getrlimit(resource.RLIMIT_AS) != (resource.RLIM_INFINITY, resource.RLIM_INFINITY):
        raise RuntimeError("ASan cannot start under the inherited address-space limit (ulimit -v); remove it, or run "
                           "without the sanitizer build")
    return run_command(
        build.name,
        [str(compiled.binary)],
        inputs,
        limits,
        build.environment(limits),
        address_space_limit=not build.sanitize,
        argv0="prog",
        work_root=work_root,
    )


def run_interpreter(
    nitwit: Path, program: Path, bits: int, inputs: Path | None, limits: Limits, work_root: Path | None = None
) -> RunResult:
    """Runs the program in the interpreter in replay mode (README.md, "Contract with the interpreter")."""
    argv = [str(nitwit), "--replay", "{inputs}", "--input-log", "{log}", f"-{bits}", str(program.resolve())]
    return run_command("nitwit", argv, inputs, limits, work_root=work_root)


def differences(a: RunResult, b: RunResult) -> list[str]:
    """Returns what differs between two runs (README.md, "Comparison"); a run that hit a limit agrees with nothing."""
    if a.limit_exceeded or b.limit_exceeded:
        return ["limit"]
    found = []
    if a.outcome() != b.outcome():
        found.append("outcome")
    if markers(a.stderr) != markers(b.stderr):
        found.append("marker")
    if a.stdout != b.stdout:
        found.append("stdout")
    if a.input_log != b.input_log:
        found.append("input-log")
    return found


def agree(a: RunResult, b: RunResult) -> bool:
    """Whether two runs agree."""
    return not differences(a, b)


def default_builds(bits: int, sanitizer_compiler: str | None = CLANG) -> list[Build]:
    """GCC, Clang with pattern initialization, plus a sanitizer build unless sanitizer_compiler is None."""
    builds = [Build(GCC, bits), Build(CLANG, bits, pattern=True)]
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
            if build == builds[0] and creates_threads(compiled.binary):
                print("the program creates threads: its runs are not compared")
                return 3
            runs.append(run_binary(compiled, args.inputs, limits, args.work_dir))
    if args.nitwit:
        runs.append(run_interpreter(args.nitwit, args.program, args.bits, args.inputs, limits, args.work_dir))

    reference = runs[0]
    findings = [run for run in runs if run.outcome() == "sanitizer"]
    limited = [run for run in runs if run.limit_exceeded]
    disagreeing = [run for run in runs[1:] if run not in findings + limited and differences(reference, run)]
    for run in runs:
        inputs_used = run.input_log.count(b"\n")
        line = f"{run.name:28} {run.outcome():16} stdout {len(run.stdout):7} B, {inputs_used:5} inputs"
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
class Expectation:
    """What every run of a semantics test must produce; None means "not checked"."""

    stdout: bytes | None
    status: str  # an outcome, see RunResult.outcome
    log: bytes | None


@dataclass
class SemanticsResult:
    """The result of one semantics test for one data model."""

    name: str
    bits: int
    problems: list[str]
    runs: list[RunResult]


def expectation_paths(program: Path, bits: int) -> tuple[Path, Path, Path]:
    """Returns the files with the expected stdout, status and input log of a semantics test (README.md)."""
    name = program.stem
    return (
        program.with_name(f"{name}.expected-{bits}"),
        program.with_name(f"{name}.expected-status"),
        program.with_name(f"{name}.expected-log-{bits}"),
    )


def read_expectation(program: Path, bits: int) -> Expectation:
    """Reads the expected files of a semantics test; a missing status file means exit:0."""
    stdout_path, status_path, log_path = expectation_paths(program, bits)
    return Expectation(
        stdout=stdout_path.read_bytes() if stdout_path.exists() else None,
        status=status_path.read_text().strip() if status_path.exists() else "exit:0",
        log=log_path.read_bytes() if log_path.exists() else None,
    )


def write_expectation(program: Path, bits: int, run: RunResult) -> None:
    """Writes the expected stdout, the status unless it is exit:0, and the input log if its file exists."""
    stdout_path, status_path, log_path = expectation_paths(program, bits)
    stdout_path.write_bytes(run.stdout)
    if run.outcome() != "exit:0":
        status_path.write_text(run.outcome() + "\n")
    elif status_path.exists():
        status_path.unlink()
    if log_path.exists():
        log_path.write_bytes(run.input_log)


def semantics_builds(bits: int) -> list[Build]:
    """GCC and Clang with pattern initialization, and both with sanitizers where the sanitizer runtime is installed."""
    builds = [Build(GCC, bits), Build(CLANG, bits, pattern=True)]
    builds += [Build(compiler, bits, sanitize=True) for compiler in COMPILERS if sanitizer_available(compiler, bits)]
    return builds


def run_problems(run: RunResult, expected: Expectation, reference: RunResult) -> list[str]:
    """Returns how a run of a semantics test deviates from the expectation and from the reference run."""
    problems = []
    if run.outcome() == "sanitizer":
        problems.append(f"{run.name}: {sanitizer_report(run.stderr)}")
    elif run.outcome() != expected.status:
        problems.append(f"{run.name}: {run.outcome()}, expected {expected.status}")
    other_stderr = [line for line in run.stderr.splitlines() if not line.startswith(MARKER)]
    if other_stderr:
        problems.append(f"{run.name}: output on stderr: {other_stderr[0][:200]!r}")
    if expected.stdout is not None and run.stdout != expected.stdout:
        problems.append(f"{run.name}: stdout differs from the expected output")
    if expected.log is not None and run.input_log != expected.log:
        problems.append(f"{run.name}: input log differs from the expected log")
    if run is not reference and differences(reference, run):
        problems.append(f"{run.name}: differs from {reference.name}: {', '.join(differences(reference, run))}")
    return problems


def check_semantics_test(
    program: Path, bits: int, limits: Limits, nitwit: Path | None, update: bool, work_dir: Path | None
) -> SemanticsResult:
    """Runs one semantics test with every build. With update, takes the expectation from the first build and writes
    it if all builds meet it."""
    inputs = program.with_suffix(".inputs")
    inputs_or_none = inputs if inputs.exists() else None
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
    if not runs:
        return SemanticsResult(program.stem, bits, problems, runs)

    expected = read_expectation(program, bits)
    if update:
        log = runs[0].input_log if expected.log is not None else None
        expected = Expectation(runs[0].stdout, runs[0].outcome(), log)
    elif expected.stdout is None:
        problems.append(f"{program.stem}.expected-{bits} is missing (use --update)")
    for run in runs:
        problems += run_problems(run, expected, runs[0])
    if update and not problems:
        write_expectation(program, bits, runs[0])
    return SemanticsResult(program.stem, bits, problems, runs)


def command_semantics(args: argparse.Namespace) -> int:
    """Checks that every build meets the expectation of every semantics test."""
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
    systemd_scope_available()

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
        """Adds the options that both commands have."""
        p.add_argument("--timeout", type=float, default=timeout, help="wall-clock limit per run in seconds")
        p.add_argument("--nitwit", type=Path, help="also run this interpreter binary in replay mode")
        p.add_argument("--work-dir", type=Path, help="directory for temporary files (default: the system's)")

    run = sub.add_parser("run", help="compile and run one program with GCC, Clang and a sanitizer build")
    run.add_argument("program", type=Path, help="the C program (.c or .i)")
    run.add_argument("--bits", type=int, choices=(32, 64), default=64, help="data model (default 64)")
    run.add_argument("--inputs", type=Path, help="inputs file, one value per line (default: no inputs)")
    run.add_argument("--memory", type=int, default=2048, help="memory limit in MiB (default 2048)")
    run.add_argument("--sanitizer-compiler", choices=COMPILERS, default=CLANG, help="compiler of the sanitizer build")
    run.add_argument("--no-sanitizer", action="store_true", help="skip the sanitizer build")
    run.add_argument("-v", "--verbose", action="store_true", help="print stdout, stderr and the input log of each run")
    add_common(run, 10.0)
    run.set_defaults(handler=command_run)

    semantics = sub.add_parser("semantics", help="check tests/semantics against GCC and Clang")
    semantics.add_argument("names", nargs="*", help="tests to run (default: all)")
    semantics.add_argument("--update", action="store_true", help="write the expected files if all builds agree")
    semantics.add_argument("--bits", type=int, nargs="+", choices=(32, 64), default=[32, 64], help="data models")
    semantics.add_argument("--directory", type=Path, default=SEMANTICS_DIR, help="test directory")
    semantics.add_argument("--jobs", type=int, default=4, help="parallel tests (default 4; each run may use 2 GiB)")
    add_common(semantics, 10.0)
    semantics.set_defaults(handler=command_semantics)

    args = parser.parse_args(argv)
    return args.handler(args)


if __name__ == "__main__":
    sys.exit(main())
