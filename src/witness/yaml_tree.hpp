#pragma once

// A compact tree of a YAML document, built from the events of yaml-cpp's parser. It needs a few times the size of the
// document in memory, where yaml-cpp's own node tree needs about 30 times. Not part of the module's API.

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "support/error.hpp"

namespace nitwit::witness {

struct YamlNode {
    enum class Kind : std::uint8_t { Null, Scalar, Sequence, Map };

    Kind kind = Kind::Null;
    std::uint32_t line = 0;         // 1-based position in the document
    std::uint32_t column = 0;       // 1-based
    std::string text;               // the value of a scalar
    std::vector<YamlNode> children; // the items of a sequence; the keys and values of a mapping, alternating

    /// The value of `key` in a mapping, or nullptr if there is none. Only scalar keys are compared.
    [[nodiscard]] const YamlNode* find(std::string_view key) const;
};

/// The tree of the single YAML document in `contents`; a Null node if there is no document. Error if the YAML is
/// malformed, there is more than one document, a mapping has a duplicate key, or the document uses an alias.
[[nodiscard]] std::expected<YamlNode, Error> readYamlTree(std::string_view contents);

} // namespace nitwit::witness
