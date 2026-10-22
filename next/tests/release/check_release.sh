#!/usr/bin/env bash
# Checks nitwit.sh, the default data model of bin/nitwit32 and bin/nitwit64, and the smoke test of the release archive
# (docs/release.md) with a nitwit executable.
# Usage: check_release.sh <nitwit> <project-version> <work-dir>
set -euo pipefail
nitwit="$(readlink -f "$1")"
version="$2"
here="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
release="$here/../../release"
# A blank in the path catches missing quotes in the scripts.
work="$3/with blank"

rm -rf "$3"
mkdir -p "$work/nitwit/bin"
cp -r "$release/nitwit.sh" "$release/smoketest.sh" "$release/smoketest" "$work/nitwit/"
chmod +x "$work/nitwit/nitwit.sh" "$work/nitwit/smoketest.sh"
ln -s "$nitwit" "$work/nitwit/bin/nitwit32"
ln -s "$nitwit" "$work/nitwit/bin/nitwit64"
ln -s nitwit/nitwit.sh "$work/linked-nitwit.sh"
cd "$work"

failures=0
fail() {
    printf 'FAIL: %s\n' "$1" >&2
    failures=$((failures + 1))
}
# expect <exit-code> <command>...: runs the command and compares its exit code.
expect() {
    local expected="$1" status=0 output
    shift
    output="$("$@" 2>&1)" || status=$?
    if [ "$status" -ne "$expected" ]; then
        fail "$(printf "'%s' exited with %d, expected %d:\n%s" "$*" "$status" "$expected" "$output")"
    fi
}

# BenchExec takes the version from nitwit.sh --version.
printed="$(nitwit/nitwit.sh --version 2>&1)" || true
if [ "$printed" != "$version" ]; then
    fail "nitwit.sh --version printed '$printed', expected '$version'"
fi

# The smoke test, started from another directory.
expect 0 nitwit/smoketest.sh

# lp64.c reaches the error function only in LP64; in ILP32 the target is reached without it (250).
data=("-w" "$here/data/lp64.yml" "$here/data/lp64.c")
expect 250 nitwit/bin/nitwit32 "${data[@]}"
expect 0 nitwit/bin/nitwit64 "${data[@]}"
expect 250 nitwit/bin/nitwit64 -32 "${data[@]}"
expect 0 nitwit/bin/nitwit32 -64 "${data[@]}"
expect 250 nitwit/nitwit.sh "${data[@]}"
expect 0 nitwit/nitwit.sh -64 "${data[@]}"
expect 250 ./linked-nitwit.sh "${data[@]}"
expect 0 ./linked-nitwit.sh -64 "${data[@]}"

# A missing executable, and a directory in its place.
rm nitwit/bin/nitwit32
expect 255 nitwit/nitwit.sh --version
mkdir nitwit/bin/nitwit32
expect 255 nitwit/nitwit.sh --version

if [ "$failures" -ne 0 ]; then
    exit 1
fi
