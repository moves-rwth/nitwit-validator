#!/usr/bin/env bash
# Configures, builds and tests the presets every hand-over requires, then checks the formatting.
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
git ls-files -co --exclude-standard -- '*.hpp' '*.cpp' | xargs -r clang-format-20 --dry-run --Werror
echo "=== all checks passed"
