#!/usr/bin/env bash

# Smoke test of the tool archive, as run by https://gitlab.com/sosy-lab/benchmarking/fm-tools:
# validates a small violation witness (format 2.x) with the 32bit and the 64bit executable.

set -e
cd "$(dirname "$0")"

./nitwit.sh --version
for ARCHITECTURE in -32 -64 ; do
  ./nitwit.sh $ARCHITECTURE --witness smoketest/smoketest.yml --property smoketest/unreach-call.prp smoketest/smoketest.c
done
echo "The smoke test finished successfully."
