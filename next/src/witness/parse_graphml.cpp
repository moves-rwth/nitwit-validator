#include <algorithm>
#include <format>
#include <iterator>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pugixml.hpp>

#include "witness/number.hpp"
#include "witness/parse.hpp"

namespace nitwit::witness {

namespace {

// Format 1.0 (README-GraphML.md): a `<data key="...">` element refers to a `<key>` declaration by its id, and the
// declaration's attr.name says what the data means. Data are read by the attr.names that CPAchecker declares
// (isEntryNode, startline, ...). The format documentation instead names the keys by CPAchecker's key ids, which
// differ from the attr.names for entry, sink, violation, sourcecodelang and programfile (returnFromFunction is named
// as the attr.name). CPAchecker's ids returnFrom and originfile differ, too. Some producers declare such ids as
// attr.name, and the key of a data element without a declaration is taken as an id, so these ids are mapped to the
// attr.names.
constexpr std::pair<std::string_view, std::string_view> keyIdAliases[] = {
    {"entry", "isEntryNode"},         {"sink", "isSinkNode"},
    {"violation", "isViolationNode"}, {"sourcecodelang", "sourcecodeLanguage"},
    {"programfile", "programFile"},   {"returnFrom", "returnFromFunction"},
    {"originfile", "originFileName"},
};

std::string_view trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n";
    const std::size_t begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(whitespace) + 1 - begin);
}

/// The trimmed character data of an element, CDATA sections included: `x <![CDATA[<]]> 5;` gives "x < 5;".
std::string textOf(const pugi::xml_node& element) {
    std::string text;
    for (const pugi::xml_node child : element.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            text += child.value();
        }
    }
    return std::string(trim(text));
}

struct KeyDeclaration {
    std::string name;
    std::string domain; // the attribute `for`: "graph", "node", "edge" or "all" (GraphML's default)
    std::optional<std::string> defaultValue;
};

/// A data element: what it means (an attr.name, with the key-id aliases resolved) and its value.
struct Datum {
    std::string_view name;
    std::string value;
};

class KeyTable {
public:
    explicit KeyTable(const pugi::xml_node& graphml) {
        for (const pugi::xml_node key : graphml.children("key")) {
            KeyDeclaration declaration{.name = key.attribute("attr.name").value(),
                                       .domain = key.attribute("for").as_string("all"),
                                       .defaultValue = std::nullopt};
            if (const pugi::xml_node defaultValue = key.child("default")) {
                declaration.defaultValue = textOf(defaultValue);
            }
            declarations_.emplace(key.attribute("id").value(), std::move(declaration));
        }
    }

    /// The data of `element`: first the defaults declared for elements of its kind, then its own data elements, so
    /// that applying them in order lets the element's data override the defaults.
    [[nodiscard]] std::vector<Datum> data(const pugi::xml_node& element, std::string_view domain) const {
        std::vector<Datum> result;
        for (const auto& [id, declaration] : declarations_) {
            if (declaration.defaultValue && (declaration.domain == domain || declaration.domain == "all")) {
                result.push_back({meaning(id), *declaration.defaultValue});
            }
        }
        for (const pugi::xml_node data : element.children("data")) {
            result.push_back({meaning(data.attribute("key").value()), textOf(data)});
        }
        return result;
    }

private:
    [[nodiscard]] std::string_view meaning(std::string_view keyId) const {
        std::string_view name = keyId;
        if (const auto found = declarations_.find(keyId); found != declarations_.end() && !found->second.name.empty()) {
            name = found->second.name;
        }
        const auto* alias =
            std::ranges::find(keyIdAliases, name, &std::pair<std::string_view, std::string_view>::first);
        return alias != std::end(keyIdAliases) ? alias->second : name;
    }

    std::map<std::string, KeyDeclaration, std::less<>> declarations_;
};

std::expected<bool, Error> parseBoolean(const Datum& datum, std::string_view where) {
    if (datum.value == "true") {
        return true;
    }
    if (datum.value == "false") {
        return false;
    }
    return std::unexpected(
        Error{std::format("{}: {} must be true or false, not '{}'", where, datum.name, datum.value)});
}

// Unlike format 2.x, whose schema requires lines of at least 1, any non-negative number is accepted, also for lines:
// the GraphML documentation gives no minimum, and NITWIT 3.x accepted 0.
std::expected<std::uint32_t, Error> parseNumber(const Datum& datum, std::string_view where) {
    if (const auto number = parseUnsigned(datum.value)) {
        return *number;
    }
    return std::unexpected(
        Error{std::format("{}: {} must be a non-negative integer, not '{}'", where, datum.name, datum.value)});
}

void readGraphData(const KeyTable& keys, const pugi::xml_node& graph, GraphmlWitness& witness) {
    for (const Datum& datum : keys.data(graph, "graph")) {
        std::string* field = nullptr;
        if (datum.name == "witness-type") {
            field = &witness.witnessType;
        } else if (datum.name == "sourcecodeLanguage") {
            field = &witness.sourceCodeLang;
        } else if (datum.name == "producer") {
            field = &witness.producer;
        } else if (datum.name == "specification") {
            field = &witness.specification;
        } else if (datum.name == "programFile") {
            field = &witness.programFile;
        } else if (datum.name == "architecture") {
            field = &witness.architecture;
        }
        if (field != nullptr) {
            *field = datum.value;
        }
    }
}

std::expected<GraphmlNode, Error> readNode(const KeyTable& keys, const pugi::xml_node& element) {
    GraphmlNode node;
    node.id = element.attribute("id").value();
    const std::string where = std::format("node '{}'", node.id);
    for (const Datum& datum : keys.data(element, "node")) {
        bool* flag = nullptr;
        if (datum.name == "isEntryNode") {
            flag = &node.isEntry;
        } else if (datum.name == "isViolationNode") {
            flag = &node.isViolation;
        } else if (datum.name == "isSinkNode") {
            flag = &node.isSink;
        }
        if (flag != nullptr) {
            const auto value = parseBoolean(datum, where);
            if (!value) {
                return std::unexpected(value.error());
            }
            *flag = *value;
        }
    }
    return node;
}

std::expected<GraphmlEdge, Error> readEdge(const KeyTable& keys, const pugi::xml_node& element,
                                           std::string_view where) {
    GraphmlEdge edge;
    edge.source = element.attribute("source").value();
    edge.target = element.attribute("target").value();
    for (const Datum& datum : keys.data(element, "edge")) {
        std::optional<std::uint32_t>* number = nullptr;
        std::optional<std::string>* text = nullptr;
        if (datum.name == "startline") {
            number = &edge.startLine;
        } else if (datum.name == "endline") {
            number = &edge.endLine;
        } else if (datum.name == "startoffset") {
            number = &edge.startOffset;
        } else if (datum.name == "endoffset") {
            number = &edge.endOffset;
        } else if (datum.name == "assumption") {
            text = &edge.assumption;
        } else if (datum.name == "assumption.scope") {
            text = &edge.assumptionScope;
        } else if (datum.name == "assumption.resultfunction") {
            text = &edge.assumptionResultFunction;
        } else if (datum.name == "control") {
            if (datum.value != "condition-true" && datum.value != "condition-false") {
                return std::unexpected(Error{std::format(
                    "{}: control must be condition-true or condition-false, not '{}'", where, datum.value)});
            }
            text = &edge.control;
        } else if (datum.name == "enterFunction") {
            text = &edge.enterFunction;
        } else if (datum.name == "returnFromFunction") {
            text = &edge.returnFromFunction;
        } else if (datum.name == "originFileName") {
            text = &edge.originFileName;
        } else if (datum.name == "threadId") {
            text = &edge.threadId;
        } else if (datum.name == "enterLoopHead") {
            const auto value = parseBoolean(datum, where);
            if (!value) {
                return std::unexpected(value.error());
            }
            edge.enterLoopHead = *value;
        }
        if (number != nullptr) {
            const auto value = parseNumber(datum, where);
            if (!value) {
                return std::unexpected(value.error());
            }
            *number = *value;
        } else if (text != nullptr) {
            *text = datum.value;
        }
    }
    return edge;
}

/// The 1-based line and column of a byte offset, for messages.
std::pair<std::size_t, std::size_t> lineColumn(std::string_view contents, std::size_t offset) {
    const std::string_view before = contents.substr(0, std::min(offset, contents.size()));
    const std::size_t lineStart = before.rfind('\n');
    const auto line = static_cast<std::size_t>(std::ranges::count(before, '\n')) + 1;
    return {line, lineStart == std::string_view::npos ? before.size() + 1 : before.size() - lineStart};
}

std::expected<GraphmlWitness, Error> readGraphml(std::string_view contents) {
    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_buffer(contents.data(), contents.size());
    if (result.status == pugi::status_out_of_memory) {
        throw std::bad_alloc();
    }
    if (!result) {
        const auto [line, column] = lineColumn(contents, static_cast<std::size_t>(result.offset));
        return std::unexpected(
            Error{std::format("malformed XML at line {}, column {}: {}", line, column, result.description())});
    }
    const pugi::xml_node graphml = document.document_element();
    if (std::string_view(graphml.name()) != "graphml") {
        return std::unexpected(Error{std::format("the root element is <{}>, not <graphml>", graphml.name())});
    }
    const pugi::xml_node graph = graphml.child("graph");
    if (!graph) {
        return std::unexpected(Error{"the <graphml> element has no <graph>"});
    }

    const KeyTable keys(graphml);
    GraphmlWitness witness;
    readGraphData(keys, graph, witness);

    std::set<std::string_view> nodeIds;
    std::size_t entryNodes = 0;
    for (const pugi::xml_node element : graph.children("node")) {
        if (!element.attribute("id")) {
            return std::unexpected(Error{std::format("node {}: missing attribute 'id'", witness.nodes.size() + 1)});
        }
        auto node = readNode(keys, element);
        if (!node) {
            return std::unexpected(node.error());
        }
        if (!nodeIds.insert(element.attribute("id").value()).second) {
            return std::unexpected(Error{std::format("duplicate node id '{}'", node->id)});
        }
        if (node->isEntry) {
            ++entryNodes;
        }
        witness.nodes.push_back(std::move(*node));
    }
    if (entryNodes != 1) {
        return std::unexpected(Error{std::format("expected exactly one entry node, found {}", entryNodes)});
    }

    for (const pugi::xml_node element : graph.children("edge")) {
        const std::string where = std::format("edge {} ('{}' -> '{}')", witness.edges.size() + 1,
                                              element.attribute("source").value(), element.attribute("target").value());
        for (const char* end : {"source", "target"}) {
            if (!element.attribute(end)) {
                return std::unexpected(Error{std::format("{}: missing attribute '{}'", where, end)});
            }
            if (!nodeIds.contains(element.attribute(end).value())) {
                return std::unexpected(Error{std::format("{}: unknown {} node", where, end)});
            }
        }
        auto edge = readEdge(keys, element, where);
        if (!edge) {
            return std::unexpected(edge.error());
        }
        witness.edges.push_back(std::move(*edge));
    }
    return witness;
}

} // namespace

std::expected<GraphmlWitness, Error> parseGraphmlWitness(std::string_view contents) {
    try {
        return readGraphml(contents);
    } catch (const std::bad_alloc&) {
        return std::unexpected(Error{"out of memory while reading the witness"});
    }
}

} // namespace nitwit::witness
