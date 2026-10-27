# Module `witness`

Reads witness files into the plain models of `model.hpp`. No knowledge of C and no decisions about support: the
validation layer (`validate`) turns the models into monitors and rejects what NITWIT cannot handle.

- `model.hpp`: format 2.x (`YamlWitness`, `ViolationSequence`, `Segment`, `Waypoint`) and format 1.0
  (`GraphmlWitness`).
- `parse.hpp`: `detectFormat`, `parseYamlWitness` (yaml-cpp), `parseGraphmlWitness` (pugixml).
- `write.hpp`: `writeYamlWitness`, a violation sequence as YAML (format 2.x), for the concrete witnesses NITWIT writes
  (`src/validate/README.md`, "Concrete witnesses"). It writes text itself, with every string quoted, and the metadata
  the model does not hold (uuid, creation time, input file hashes) from a `Provenance`.

The validation of format 1.0 is not implemented (`docs/roadmap.md`): the driver gives GraphML witnesses UNKNOWN
without parsing them, and only the tests use `parseGraphmlWitness`.

## References

- Format 2.x: https://gitlab.com/sosy-lab/benchmarking/sv-witnesses (README and the JSON schemas of 2.0 and 2.1), and
  the paper "Software Verification Witnesses 2.0" (SPIN 2024).
- Format 1.0: the same repository, `README-GraphML.md`.
- NITWIT 3.x: `witness/witness_yaml.cpp` and `witness/automaton_parse.cpp` on the branch `old` show what the old
  implementation accepted.

## Tests

- `tests/unit/witness/`: every waypoint type and action; optional fields; all schema errors listed in `parse.hpp`;
  the escape repair; multiple and missing `violation_sequence` entries; GraphML key resolution through `<key>`
  declarations, entry/violation/sink nodes, all edge keys, malformed XML. Small witness files live in `data/`.
  `yaml_write_test.cpp`: written witnesses read back by the parser (every waypoint type and action, the metadata,
  strings with quotes, backslashes, blanks and control characters).
- `tests/corpus/witness/` (label `corpus`): parses all witnesses in `NITWIT_DATASET_DIR/witnesses` and the
  witnesses in `testfiles/`; reports parse errors grouped by message. The largest corpus witness (235 MB) parses in
  12.6 s with 1.5 GB.
