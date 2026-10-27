#pragma once

// Writes violation witnesses in format 2.x (YAML), the counterpart of parseYamlWitness for the witnesses NITWIT
// produces (validate/README.md, "Concrete witnesses").

#include <string>
#include <vector>

#include "witness/model.hpp"

namespace nitwit::witness {

/// The metadata a written witness needs beyond the model (witness format 2.x, "metadata").
struct Provenance {
    std::string uuid;         // RFC 9562 form, e.g. "1b4e28ba-2fa1-81d2-883f-0016d3cca427"
    std::string creationTime; // ISO 8601, e.g. "2026-10-27T12:00:00Z"
    /// The SHA-256 digest of each file of Task::inputFiles, in the same order, as 64 lowercase hexadecimal digits.
    std::vector<std::string> inputFileHashes;
};

/// A YAML document with one entry, the violation sequence, in the order and with the keys of the format's schema. The
/// format version is the metadata's. Fields of the model that are empty or not set are left out. Strings are quoted:
/// in single quotes, or in double quotes with escape sequences where they contain control characters, so that YAML
/// reads them back unchanged (YAML 1.2.2, sections 7.3.2 and 7.3.1).
[[nodiscard]] std::string writeYamlWitness(const ViolationSequence& sequence, const Provenance& provenance);

} // namespace nitwit::witness
