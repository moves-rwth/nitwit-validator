#!/usr/bin/env bash
# Builds the release of NITWIT and packs the archive nitwit-<version>.zip for BenchExec and fm-tools, with the single
# top-level directory nitwit/ (docs/release.md).
# Usage: tools/package.sh [output-dir]   (default: the build directory)
set -euo pipefail

source="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd -P)"
# The binaryDir of the preset release (CMakePresets.json).
build="$HOME/nitwit-build$source/release"
output="$(mkdir -p "${1:-$build}" && cd "${1:-$build}" && pwd -P)"

cd "$source"
# The project version; tests/release/check_release.sh checks that nitwit --version prints it.
version="$(sed -n 's/^project(nitwit VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
cmake --preset release -B "$build" > /dev/null
cmake --build "$build" --target nitwit
nitwit="$build/src/driver/nitwit"

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT
# --glibc-only also fails if nitwit needs a newer glibc than the documentation promises (maxGlibc there).
bash tests/release/check_release.sh "$nitwit" "$version" "$staging/check" --glibc-only

archive="$staging/nitwit"
mkdir -p "$archive/bin" "$archive/smoketest" "$archive/licenses"
# Two plain copies rather than symlinks: zip archives and BenchExec handle plain files best.
install -m 755 "$nitwit" "$archive/bin/nitwit32"
install -m 755 "$nitwit" "$archive/bin/nitwit64"
install -m 755 release/nitwit.sh release/smoketest.sh "$archive/"
install -m 644 release/README.md release/THIRD-PARTY.md "$archive/"
install -m 644 release/smoketest/* "$archive/smoketest/"
install -m 644 LICENSE "$archive/LICENSE"

# The notices of the statically linked code (THIRD-PARTY.md).
install -m 644 release/licenses/* "$archive/licenses/"
install -m 644 "$build/_deps/yaml-cpp-src/LICENSE" "$archive/licenses/yaml-cpp-LICENSE"
install -m 644 "$build/_deps/pugixml-src/LICENSE.md" "$archive/licenses/pugixml-LICENSE.md"

# "<package> <version>" of the Debian package that provides a file, or "unknown".
packageOf() {
    local package
    package="$(dpkg-query -S "$(readlink -f "$1")" 2> /dev/null | head -n 1 | sed 's/: .*//')" || true
    if [ -n "$package" ]; then
        echo "$package $(dpkg-query -W -f "\${Version}" "$package")"
    else
        echo unknown
    fi
}
compiler="$(sed -n 's/^CMAKE_CXX_COMPILER:[A-Z]*=//p' "$build/CMakeCache.txt")"
# The newest glibc symbol version nitwit uses is the oldest glibc it runs with.
glibc="$(objdump -T "$nitwit" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -u -V | tail -n 1)"
if ! revision="$(git -C "$source" describe --always --dirty 2> /dev/null)"; then
    revision=unknown
    repository="$source"
    while [ "$repository" != / ] && [ ! -e "$repository/.git" ]; do
        repository="$(dirname "$repository")"
    done
    if [ -e "$repository/.git" ]; then
        echo "package.sh: warning: git describe fails in $repository; the source revision is unknown" >&2
    fi
fi
{
    echo "NITWIT $version, source revision $revision"
    echo "compiler: $("$compiler" --version | head -n 1)"
    echo "libstdc++ (static): $(packageOf "$("$compiler" -print-file-name=libstdc++.a)")"
    echo "libgcc (static): $(packageOf "$("$compiler" -print-libgcc-file-name)")"
    echo "glibc (shared): at least $glibc; built with $(packageOf "$("$compiler" -print-file-name=libc.so)")"
} > "$archive/licenses/VERSIONS"

"$archive/smoketest.sh"

zip="$output/nitwit-$version.zip"
rm -f "$zip"
(cd "$staging" && cmake -E tar cf "$zip" --format=zip nitwit)
echo "$zip"
