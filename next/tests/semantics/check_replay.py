#!/usr/bin/env python3
"""Runs one program of the semantics suite with `nitwit --replay` and checks it against the expected files that
tools/difftest/difftest.py maintains (tools/difftest/README.md, "Semantics tests"): the output, the outcome, the input
log, and that stderr holds only the harness's markers. For an expected error, the marker (which names the error
function) is compared with that of a native GCC run.

Usage: check_replay.py <nitwit> <program.c> <32|64>
"""

import signal
import subprocess
import sys
import tempfile
from pathlib import Path

MARKER = b"NITWIT-DIFFTEST: "
TIMEOUT_SECONDS = 120
DIFFTEST_DIR = Path(__file__).resolve().parents[2] / "tools" / "difftest"


def outcome(status: int, stderr: bytes) -> str:
    """Classifies a run like RunResult.outcome in tools/difftest/difftest.py."""
    if status < 0:
        return "abort" if -status == signal.SIGABRT else f"signal:{signal.Signals(-status).name}"
    if status == 97 and MARKER + b"error" in stderr:
        return "error"
    if status == 98 and MARKER + b"assume" in stderr:
        return "assume"
    if status == 99 and MARKER + b"bad input" in stderr:
        return "bad-input"
    return f"exit:{status}"


def native_markers(program: Path, bits: str, inputs: Path | None) -> list[bytes]:
    """The harness's marker lines of the program compiled with GCC (tools/difftest/difftest.py)."""
    sys.path.insert(0, str(DIFFTEST_DIR))
    import difftest  # noqa: PLC0415 - only needed for the error tests

    limits = difftest.Limits(timeout=TIMEOUT_SECONDS)
    with tempfile.TemporaryDirectory(prefix="nitwit-semantics-native-") as directory:
        compiled = difftest.compile_program(program, difftest.Build(difftest.GCC, int(bits)), Path(directory), limits)
        if compiled.binary is None:
            return [b"native compile failure: " + compiled.message.encode()]
        run = difftest.run_binary(compiled, inputs, limits)
    return difftest.markers(run.stderr)


def main(nitwit: str, program: Path, bits: str) -> int:
    inputs = program.with_suffix(".inputs")
    expected_stdout = program.with_name(f"{program.stem}.expected-{bits}").read_bytes()
    status_path = program.with_name(f"{program.stem}.expected-status")
    expected_status = status_path.read_text().strip() if status_path.exists() else "exit:0"
    log_path = program.with_name(f"{program.stem}.expected-log-{bits}")

    with tempfile.TemporaryDirectory(prefix="nitwit-semantics-") as directory:
        log = Path(directory) / "input.log"
        command = [nitwit, "--replay", str(inputs) if inputs.exists() else "", "--input-log", str(log), f"-{bits}",
                   str(program)]
        run = subprocess.run(command, capture_output=True, timeout=TIMEOUT_SECONDS, cwd=directory)
        actual_log = log.read_bytes() if log.exists() else b""

    problems = []
    actual_status = outcome(run.returncode, run.stderr)
    if actual_status != expected_status:
        problems.append(f"outcome {actual_status}, expected {expected_status}")
    other_stderr = [line for line in run.stderr.splitlines() if not line.startswith(MARKER)]
    if other_stderr:
        problems.append("stderr: " + b"\n".join(other_stderr).decode(errors="replace"))
    if run.stdout != expected_stdout:
        problems.append("stdout differs:\n--- expected\n" + expected_stdout.decode(errors="replace")
                        + "--- actual\n" + run.stdout.decode(errors="replace"))
    if log_path.exists() and actual_log != log_path.read_bytes():
        problems.append("input log differs:\n--- expected\n" + log_path.read_text(errors="replace")
                        + "--- actual\n" + actual_log.decode(errors="replace"))
    if expected_status == "error":
        # The expected files keep no stderr: the marker names the error function, which a native run shows.
        expected_markers = native_markers(program, bits, inputs if inputs.exists() else None)
        actual_markers = [line for line in run.stderr.splitlines() if line.startswith(MARKER)]
        if actual_markers != expected_markers:
            problems.append(f"markers {actual_markers}, expected {expected_markers}")
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], Path(sys.argv[2]), sys.argv[3]))
