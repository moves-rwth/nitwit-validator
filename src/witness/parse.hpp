#pragma once

// Parsers for witness files. They check the structure of the file (keys, types, allowed values) but not whether
// NITWIT supports what the witness asks for; that is the validation layer's decision. Running out of memory while
// reading a witness is reported as an Error, too.

#include <cstdint>
#include <expected>
#include <string_view>

#include "support/error.hpp"
#include "witness/model.hpp"

namespace nitwit::witness {

enum class WitnessFormat : std::uint8_t { Graphml, Yaml };

/// GraphML if the contents start with a UTF-16 byte order mark (pugixml detects the encoding), or if the first
/// character that is not whitespace or a UTF-8 byte order mark is '<'; YAML otherwise.
[[nodiscard]] WitnessFormat detectFormat(std::string_view contents);

/// Parses a witness in format 2.x (2.0 to 2.2).
/// - Error if the contents are not exactly one YAML document holding a sequence of entries, use aliases or duplicate
///   keys, an entry lacks `entry_type`, or a `violation_sequence` entry breaks the schema, for example: unknown
///   waypoint type or action; missing location, file name or line; line or column ≤ 0; a constraint without value; a
///   negative `thread_id`. The list is not exhaustive. Messages give the entry, segment and waypoint, and the line and
///   column in the witness.
/// - Lenient where producers are: metadata may be missing (the example of the SPIN 2024 paper has none), and keys
///   that the schema does not know are ignored.
/// - Not checked here but by the validation layer: the format version, which constraints and constraint formats a
///   waypoint type needs or allows, that `target` waypoints have the action `follow`, and the composition of segments.
/// - Repairs constraint values mangled by YAML double-quote escapes, as written by some producers: a carriage return
///   followed by "esult" becomes "\result", a bell character followed by "t(" becomes "\at(". Any other control
///   character except tab and newline in a constraint value is an error.
[[nodiscard]] std::expected<YamlWitness, Error> parseYamlWitness(std::string_view contents);

/// Parses a GraphML witness in format 1.0. Data keys are resolved through the `<key>` declarations (attr.name), so
/// producers that use other key ids work; declared defaults apply. Error if the XML is malformed, a node id is
/// duplicated, an edge refers to an unknown node, there is not exactly one entry node, or a value of a key NITWIT
/// reads is malformed (booleans, numbers, `control`). The list is not exhaustive.
[[nodiscard]] std::expected<GraphmlWitness, Error> parseGraphmlWitness(std::string_view contents);

} // namespace nitwit::witness
