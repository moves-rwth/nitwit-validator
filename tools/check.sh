#!/usr/bin/env bash
# Configures, builds and tests the presets every change must pass, then checks the formatting.
# Usage: tools/check.sh [preset...]   (default: gcc-debug clang-debug asan)
set -euo pipefail
cd "$(dirname "$0")/.."

presets=("$@")
if [ ${#presets[@]} -eq 0 ]; then
    presets=(gcc-debug clang-debug asan)
fi

for preset in "${presets[@]}"; do
    echo "=== $preset"
    cmake --preset "$preset" > /dev/null
    cmake --build --preset "$preset" | grep -v '^\[' || true
    test "${PIPESTATUS[0]}" -eq 0
    ctest --preset "$preset" -LE 'corpus|difftest' | tail -n 3
    test "${PIPESTATUS[0]}" -eq 0
done

echo "=== clang-format"
# find instead of git ls-files: in WSL, git cannot follow the Windows path in a worktree's .git file.
find src tests tools -name '*.hpp' -o -name '*.cpp' | xargs -r clang-format-20 --dry-run --Werror
echo "=== all checks passed"
