Vendored copy of [yaml-cpp](https://github.com/jbeder/yaml-cpp) 0.8.0 (tag `0.8.0`, MIT license, see `LICENSE`),
used to parse YAML (format 2.x) witnesses.

Only `include/` and `src/` are kept; the optional `contrib/` graph builder was removed.
The sources are compiled directly into the NITWIT executables (see `CMakeLists.txt`) with `YAML_CPP_STATIC_DEFINE`.
