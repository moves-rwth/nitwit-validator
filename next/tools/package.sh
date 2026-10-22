#!/usr/bin/env bash
# Builds the static release of NITWIT and packs the archive nitwit-<version>.zip for BenchExec and fm-tools, with the
# single top-level directory nitwit/ (docs/release.md).
# Usage: tools/package.sh [output-dir]   (default: the build directory)
set -euo pipefail

source="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd -P)"
# The binaryDir of the preset release-static (CMakePresets.json).
build="$HOME/nitwit-next-build$(dirname "$source")/release-static"
output="$(mkdir -p "${1:-$build}" && cd "${1:-$build}" && pwd -P)"

cd "$source"
# The project version; tests/release/check_release.sh checks that nitwit --version prints it.
version="$(sed -n 's/^project(nitwit VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
cmake --preset release-static -B "$build" > /dev/null
cmake --build "$build" --target nitwit
nitwit="$build/src/driver/nitwit"

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT
bash tests/release/check_release.sh "$nitwit" "$version" "$staging/check"

archive="$staging/nitwit"
mkdir -p "$archive/bin" "$archive/smoketest" "$archive/licenses"
# Two plain copies rather than symlinks: zip archives and BenchExec handle plain files best.
install -m 755 "$nitwit" "$archive/bin/nitwit32"
install -m 755 "$nitwit" "$archive/bin/nitwit64"
install -m 755 release/nitwit.sh release/smoketest.sh "$archive/"
install -m 644 release/README.md release/THIRD-PARTY.md "$archive/"
install -m 644 release/smoketest/* "$archive/smoketest/"
# The repository's license; next/ has none of its own.
install -m 644 ../LICENSE "$archive/LICENSE"

# The notices of the statically linked code (THIRD-PARTY.md).
install -m 644 release/licenses/* "$archive/licenses/"
install -m 644 "$build/_deps/yaml-cpp-src/LICENSE" "$archive/licenses/yaml-cpp-LICENSE"
install -m 644 "$build/_deps/pugixml-src/LICENSE.md" "$archive/licenses/pugixml-LICENSE.md"
install -m 644 /usr/share/doc/libc6/copyright "$archive/licenses/glibc-copyright"
install -m 644 /usr/share/common-licenses/LGPL-2.1 "$archive/licenses/LGPL-2.1"

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
{
    echo "NITWIT $version, source revision $(git -C "$source" describe --always --dirty 2> /dev/null || echo unknown)"
    echo "compiler: $("$compiler" --version | head -n 1)"
    echo "glibc: $(packageOf "$("$compiler" -print-file-name=libc.a)")"
    echo "libstdc++: $(packageOf "$("$compiler" -print-file-name=libstdc++.a)")"
    echo "libgcc: $(packageOf "$("$compiler" -print-libgcc-file-name)")"
} > "$archive/licenses/VERSIONS"

"$archive/smoketest.sh"

zip="$output/nitwit-$version.zip"
rm -f "$zip"
(cd "$staging" && cmake -E tar cf "$zip" --format=zip nitwit)
echo "$zip"
