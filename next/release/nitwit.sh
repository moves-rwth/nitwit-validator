#!/usr/bin/env bash
# Runs NITWIT with all arguments unchanged; BenchExec's tool-info module nitwit.py calls this script.
# bin/nitwit32 reads -32 and -64 itself. bin/nitwit64 is the same program, there for nitwit.py's REQUIRED_PATHS and
# for direct calls, in which it defaults to LP64.

executable="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/bin/nitwit32"
if [ ! -f "$executable" ] || [ ! -x "$executable" ]; then
    echo "nitwit.sh: the executable $executable is missing or not executable" >&2
    exit 255
fi
exec "$executable" "$@"
