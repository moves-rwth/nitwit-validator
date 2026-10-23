#!/usr/bin/env bash
# Smoke test of the release archive, as run by fm-weck (https://gitlab.com/sosy-lab/benchmarking/fm-tools): validates
# a small violation witness in both data models through nitwit.sh, with the arguments in the order of BenchExec's
# tool-info module nitwit.py: options, property, data model (in the maintainer's fork), witness, program.

set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

./nitwit.sh --version
for dataModel in -32 -64; do
    status=0
    output="$(./nitwit.sh -p smoketest/unreach-call.prp "$dataModel" -w smoketest/smoketest.yml smoketest/smoketest.c)" ||
        status=$?
    printf '%s\n' "$output"
    if [ "$status" -ne 0 ] || ! grep -q '^VALIDATED:' <<<"$output"; then
        echo "The smoke test failed with $dataModel (exit code $status)." >&2
        exit 1
    fi
done
echo "The smoke test finished successfully."
