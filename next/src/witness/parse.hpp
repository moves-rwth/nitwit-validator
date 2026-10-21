#pragma once

// Parsers for witness files. They check the structure of the file (keys, types, allowed values) but not whether
// NITWIT supports what the witness asks for; that is the validation layer's decision.

#include <expected>
#include <string_view>

#include "support/error.hpp"
#include "witness/model.hpp"

namespace nitwit::witness {

enum class WitnessFormat : std::uint8_t { Graphml, Yaml };

/// GraphML if the first character that is not whitespace or a UTF-8 byte order mark is '<', YAML otherwise.
[[nodiscard]] WitnessFormat detectFormat(std::string_view contents);

/// Parses a witness in format 2.0 or 2.1.
/// - Error if the document is not a YAML sequence of entries, an entry lacks `entry_type`, or a `violation_sequence`
///   entry breaks the schema: unknown waypoint type or action, missing or non-positive line, column ≤ 0, a constraint
///   without value, a negative `thread_id`.
/// - Repairs constraint values mangled by YAML double-quote escapes, as written by some producers: a carriage return
///   followed by "esult" becomes "\result", a bell character followed by "t(" becomes "\at(".
[[nodiscard]] std::expected<YamlWitness, Error> parseYamlWitness(std::string_view contents);

/// Parses a GraphML witness in format 1.0. Data keys are resolved through the `<key>` declarations (attr.name), so
/// producers that use other key ids work. Error if the XML is malformed, a node id is duplicated, an edge refers to an
/// unknown node, or there is not exactly one entry node.
[[nodiscard]] std::expected<GraphmlWitness, Error> parseGraphmlWitness(std::string_view contents);

} // namespace nitwit::witness
