#include "witness/yaml_tree.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <span>
#include <spanstream>
#include <utility>

#include <yaml-cpp/anchor.h>
#include <yaml-cpp/emitterstyle.h>
#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/mark.h>
#include <yaml-cpp/parser.h>

namespace nitwit::witness {

namespace {

// A problem in the document that yaml-cpp does not report itself; readYamlTree turns it into an Error.
struct TreeError {
    std::string message;
};

std::string positioned(std::uint32_t line, std::uint32_t column, std::string_view problem) {
    return std::format("line {}, column {}: {}", line, column, problem);
}

// YAML 1.2.2, section 3.2.1.1: the keys of a mapping are unique. Only scalar keys are compared; the witness formats
// use no others.
void checkUniqueKeys(const YamlNode& map) {
    std::vector<const YamlNode*> keys;
    for (std::size_t i = 0; i < map.children.size(); i += 2) {
        if (map.children[i].kind == YamlNode::Kind::Scalar) {
            keys.push_back(&map.children[i]);
        }
    }
    const auto keyText = [](const YamlNode* key) -> const std::string& {
        return key->text;
    };
    std::ranges::stable_sort(keys, {}, keyText);
    if (const auto duplicate = std::ranges::adjacent_find(keys, {}, keyText); duplicate != keys.end()) {
        const YamlNode& second = **std::next(duplicate);
        throw TreeError{positioned(second.line, second.column,
                                   std::format("duplicate key '{}' (YAML requires unique keys)", second.text))};
    }
}

class TreeBuilder final : public YAML::EventHandler {
public:
    [[nodiscard]] YamlNode takeRoot() { return std::move(root_); }

    void OnDocumentStart(const YAML::Mark& mark) override {
        if (++documents_ > 1) {
            throw TreeError{positioned(lineOf(mark), columnOf(mark), "the witness must be a single YAML document")};
        }
    }

    void OnDocumentEnd() override {}

    void OnNull(const YAML::Mark& mark, YAML::anchor_t /*anchor*/) override { add(YamlNode::Kind::Null, mark); }

    // Aliases are rejected: they let a small file stand for a huge tree ("billion laughs"), and no witness of the
    // SV-COMP 2026 corpus uses them.
    void OnAlias(const YAML::Mark& mark, YAML::anchor_t /*anchor*/) override {
        throw TreeError{positioned(lineOf(mark), columnOf(mark), "YAML aliases are not supported")};
    }

    void OnScalar(const YAML::Mark& mark, const std::string& /*tag*/, YAML::anchor_t /*anchor*/,
                  const std::string& value) override {
        add(YamlNode::Kind::Scalar, mark).text = value;
    }

    void OnSequenceStart(const YAML::Mark& mark, const std::string& /*tag*/, YAML::anchor_t /*anchor*/,
                         YAML::EmitterStyle::value /*style*/) override {
        open_.push_back(&add(YamlNode::Kind::Sequence, mark));
    }

    void OnSequenceEnd() override { close(); }

    void OnMapStart(const YAML::Mark& mark, const std::string& /*tag*/, YAML::anchor_t /*anchor*/,
                    YAML::EmitterStyle::value /*style*/) override {
        open_.push_back(&add(YamlNode::Kind::Map, mark));
    }

    void OnMapEnd() override {
        checkUniqueKeys(*open_.back());
        close();
    }

private:
    static std::uint32_t lineOf(const YAML::Mark& mark) { return static_cast<std::uint32_t>(mark.line + 1); }
    static std::uint32_t columnOf(const YAML::Mark& mark) { return static_cast<std::uint32_t>(mark.column + 1); }

    YamlNode& add(YamlNode::Kind kind, const YAML::Mark& mark) {
        YamlNode& node = open_.empty() ? root_ : open_.back()->children.emplace_back();
        node.kind = kind;
        node.line = lineOf(mark);
        node.column = columnOf(mark);
        return node;
    }

    void close() {
        open_.back()->children.shrink_to_fit();
        open_.pop_back();
    }

    YamlNode root_;
    // The sequences and mappings being read, innermost last. Only the innermost one gets new children, so the
    // pointers to the others stay valid.
    std::vector<YamlNode*> open_;
    int documents_ = 0;
};

} // namespace

const YamlNode* YamlNode::find(std::string_view key) const {
    for (std::size_t i = 0; i + 1 < children.size(); i += 2) {
        if (children[i].kind == Kind::Scalar && children[i].text == key) {
            return &children[i + 1];
        }
    }
    return nullptr;
}

std::expected<YamlNode, Error> readYamlTree(std::string_view contents) {
    TreeBuilder builder;
    try {
        std::ispanstream stream(std::span<const char>(contents.data(), contents.size()));
        YAML::Parser parser(stream);
        if (parser.HandleNextDocument(builder)) {
            parser.HandleNextDocument(builder); // the builder throws if a second document starts
        }
    } catch (const TreeError& error) {
        return std::unexpected(Error{error.message});
    } catch (const YAML::Exception& exception) {
        if (exception.mark.is_null()) {
            return std::unexpected(Error{std::format("invalid YAML: {}", exception.msg)});
        }
        return std::unexpected(Error{std::format("invalid YAML at line {}, column {}: {}", exception.mark.line + 1,
                                                 exception.mark.column + 1, exception.msg)});
    }
    return builder.takeRoot();
}

} // namespace nitwit::witness
