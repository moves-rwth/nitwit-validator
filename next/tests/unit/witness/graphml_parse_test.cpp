#include <format>
#include <string>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_data.hpp"
#include "witness/parse.hpp"

namespace nitwit::witness {
namespace {

using testing::HasSubstr;

/// A GraphML document with the given key declarations and graph contents.
std::string graphml(std::string_view keys, std::string_view graph) {
    return std::format("<?xml version=\"1.0\"?>\n<graphml>\n{}\n<graph edgedefault=\"directed\">\n{}\n</graph>\n"
                       "</graphml>\n",
                       keys, graph);
}

constexpr std::string_view entryKey = R"(<key attr.name="isEntryNode" attr.type="boolean" for="node" id="entry"/>)";

/// A graph of the nodes A (entry) and B, and the edge A -> B with the given data elements.
std::string edgeGraph(std::string_view edgeData) {
    return std::format("<node id=\"A\"><data key=\"entry\">true</data></node>\n<node id=\"B\"/>\n"
                       "<edge source=\"A\" target=\"B\">{}</edge>",
                       edgeData);
}

/// The error message of parsing `contents`; fails the test if it parses.
std::string errorOf(std::string_view contents) {
    const auto witness = parseGraphmlWitness(contents);
    EXPECT_FALSE(witness.has_value()) << "parsed: " << contents;
    return witness ? std::string{} : witness.error().message;
}

TEST(GraphmlParseTest, ReadsTheGraphData) {
    const auto witness = parseGraphmlWitness(readTestFile("violation.graphml"));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->witnessType, "violation_witness");
    EXPECT_EQ(witness->sourceCodeLang, "C");
    EXPECT_EQ(witness->producer, "CPAchecker 2.3");
    EXPECT_EQ(witness->specification, "CHECK( init(main()), LTL(G ! call(reach_error())) )");
    EXPECT_EQ(witness->programFile, "/tmp/program.c");
    EXPECT_EQ(witness->architecture, "32bit");
}

TEST(GraphmlParseTest, ReadsEntryViolationAndSinkNodes) {
    const auto witness = parseGraphmlWitness(readTestFile("violation.graphml"));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    ASSERT_EQ(witness->nodes.size(), 5u);
    const auto& nodes = witness->nodes;
    EXPECT_EQ(nodes[0].id, "N1");
    EXPECT_TRUE(nodes[0].isEntry);
    EXPECT_FALSE(nodes[0].isViolation);
    EXPECT_FALSE(nodes[0].isSink);
    EXPECT_EQ(nodes[1].id, "N2");
    EXPECT_FALSE(nodes[1].isEntry || nodes[1].isViolation || nodes[1].isSink);
    EXPECT_EQ(nodes[3].id, "sink");
    EXPECT_TRUE(nodes[3].isSink);
    EXPECT_EQ(nodes[4].id, "N4");
    EXPECT_TRUE(nodes[4].isViolation);
}

TEST(GraphmlParseTest, ReadsEveryEdgeKey) {
    const auto witness = parseGraphmlWitness(readTestFile("violation.graphml"));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    ASSERT_EQ(witness->edges.size(), 4u);
    const auto& edges = witness->edges;

    EXPECT_EQ(edges[0].source, "N1");
    EXPECT_EQ(edges[0].target, "N2");
    EXPECT_EQ(edges[0].enterFunction, "main");
    EXPECT_EQ(edges[0].threadId, "0");
    EXPECT_EQ(edges[0].startLine, 7u);
    EXPECT_EQ(edges[0].endLine, 7u);
    EXPECT_EQ(edges[0].startOffset, 120u);
    EXPECT_EQ(edges[0].endOffset, 135u);
    EXPECT_EQ(edges[0].assumption, std::nullopt);
    EXPECT_EQ(edges[0].control, std::nullopt);

    EXPECT_EQ(edges[1].assumption, "\\result == 5;");
    EXPECT_EQ(edges[1].assumptionScope, "main");
    EXPECT_EQ(edges[1].assumptionResultFunction, "__VERIFIER_nondet_int");
    EXPECT_EQ(edges[1].endOffset, std::nullopt);

    EXPECT_EQ(edges[2].control, "condition-false");
    EXPECT_TRUE(edges[2].enterLoopHead);
    EXPECT_FALSE(edges[1].enterLoopHead);

    EXPECT_EQ(edges[3].control, "condition-true");
    EXPECT_EQ(edges[3].returnFromFunction, "main");
}

TEST(GraphmlParseTest, DeclaredDefaultsApplyUnlessOverridden) {
    const auto witness = parseGraphmlWitness(readTestFile("violation.graphml"));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->edges.at(0).originFileName, "program.c");
    EXPECT_EQ(witness->edges.at(3).originFileName, "header.h");
}

TEST(GraphmlParseTest, KeysAreResolvedThroughTheirAttrName) {
    const std::string keys = R"(<key attr.name="isEntryNode" attr.type="boolean" for="node" id="d0"/>
<key attr.name="isViolationNode" attr.type="boolean" for="node" id="d1"/>
<key attr.name="startline" attr.type="int" for="edge" id="d2"/>
<key attr.name="producer" attr.type="string" for="graph" id="d3"/>)";
    const std::string graph = R"(<data key="d3">Tool</data>
<node id="A"><data key="d0">true</data></node>
<node id="B"><data key="d1">true</data></node>
<edge source="A" target="B"><data key="d2">42</data></edge>)";
    const auto witness = parseGraphmlWitness(graphml(keys, graph));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->producer, "Tool");
    EXPECT_TRUE(witness->nodes.at(0).isEntry);
    EXPECT_TRUE(witness->nodes.at(1).isViolation);
    EXPECT_EQ(witness->edges.at(0).startLine, 42u);
}

TEST(GraphmlParseTest, KeyIdsOfTheDocumentationWorkAsNames) {
    // Some producers declare the documentation's key ids as attr.name; others do not declare their keys at all.
    const std::string declared = R"(<key attr.name="entry" attr.type="boolean" for="node" id="entry"/>
<key attr.name="sourcecodelang" attr.type="string" for="graph" id="sourcecodelang"/>)";
    for (const std::string& keys : {declared, std::string{}}) {
        const std::string graph = R"(<data key="sourcecodelang">C</data>
<data key="programfile">a.c</data>
<node id="A"><data key="entry">true</data></node>
<node id="B"><data key="violation">true</data><data key="sink">false</data></node>
<edge source="A" target="B"><data key="returnFrom">f</data><data key="originfile">a.c</data></edge>)";
        const auto witness = parseGraphmlWitness(graphml(keys, graph));
        ASSERT_TRUE(witness.has_value()) << witness.error().message;
        EXPECT_EQ(witness->sourceCodeLang, "C");
        EXPECT_EQ(witness->programFile, "a.c");
        EXPECT_TRUE(witness->nodes.at(0).isEntry);
        EXPECT_TRUE(witness->nodes.at(1).isViolation);
        EXPECT_EQ(witness->edges.at(0).returnFromFunction, "f");
        EXPECT_EQ(witness->edges.at(0).originFileName, "a.c");
    }
}

TEST(GraphmlParseTest, DefaultsOnlyApplyToTheirDomain) {
    const std::string keys = std::format("{}\n{}", entryKey,
                                         R"(<key attr.name="assumption" for="edge" id="a"><default>x</default></key>
<key attr.name="enterFunction" for="all" id="f"><default>g</default></key>
<key attr.name="isSinkNode" for="edge" id="s"><default>true</default></key>)");
    const auto witness = parseGraphmlWitness(graphml(keys, edgeGraph("")));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->edges.at(0).assumption, "x");
    EXPECT_EQ(witness->edges.at(0).enterFunction, "g");
    EXPECT_FALSE(witness->nodes.at(1).isSink);
}

TEST(GraphmlParseTest, KeysWithoutForApplyToAllElements) {
    // GraphML: the attribute `for` defaults to "all".
    const std::string keys = std::format("{}\n{}", entryKey,
                                         R"(<key attr.name="enterFunction" id="f"><default>g</default></key>
<key attr.name="isSinkNode" id="s"><default>true</default></key>)");
    const auto witness = parseGraphmlWitness(graphml(keys, edgeGraph("")));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->edges.at(0).enterFunction, "g");
    EXPECT_TRUE(witness->nodes.at(1).isSink);
}

TEST(GraphmlParseTest, CdataSectionsArePartOfTheValue) {
    const auto witness = parseGraphmlWitness(graphml(
        entryKey, edgeGraph("<data key=\"assumption\">x <![CDATA[<]]> 5 &amp;&amp; y <![CDATA[>]]> 1;</data>")));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->edges.at(0).assumption, "x < 5 && y > 1;");
}

TEST(GraphmlParseTest, ControlMustBeConditionTrueOrFalse) {
    for (const char* control : {"condition-true", "condition-false"}) {
        const auto witness =
            parseGraphmlWitness(graphml(entryKey, edgeGraph(std::format("<data key=\"control\">{}</data>", control))));
        ASSERT_TRUE(witness.has_value()) << witness.error().message;
        EXPECT_EQ(witness->edges.at(0).control, control);
    }
    EXPECT_EQ(errorOf(graphml(entryKey, edgeGraph("<data key=\"control\">true</data>"))),
              "edge 1 ('A' -> 'B'): control must be condition-true or condition-false, not 'true'");
}

TEST(GraphmlParseTest, ReadsUtf16WithByteOrderMark) {
    const std::string utf8 = graphml(entryKey, edgeGraph("<data key=\"enterFunction\">main</data>"));
    std::string utf16 = "\xFF\xFE"; // little endian
    for (const char c : utf8) {
        utf16 += c;
        utf16 += '\0';
    }
    ASSERT_EQ(detectFormat(utf16), WitnessFormat::Graphml);
    const auto witness = parseGraphmlWitness(utf16);
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->edges.at(0).enterFunction, "main");
}

TEST(GraphmlParseTest, ValuesAreTrimmed) {
    const auto witness = parseGraphmlWitness(graphml(entryKey, edgeGraph("<data key=\"startline\">\n  12\n</data>"
                                                                         "<data key=\"assumption\"> x == 1; </data>")));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->edges.at(0).startLine, 12u);
    EXPECT_EQ(witness->edges.at(0).assumption, "x == 1;");
}

TEST(GraphmlParseTest, UnknownKeysAreIgnored) {
    const auto witness = parseGraphmlWitness(
        graphml(entryKey, edgeGraph("<data key=\"sourcecode\">x = 1;</data><data key=\"lineCols\">1,2</data>")));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->edges.size(), 1u);
}

TEST(GraphmlParseTest, MalformedXmlIsAnErrorWithItsPosition) {
    EXPECT_THAT(errorOf("<graphml>\n<graph>\n<node id=\"A\">\n</graph>\n</graphml>\n"),
                HasSubstr("malformed XML at line 4"));
    EXPECT_EQ(errorOf(""), "malformed XML at line 1, column 1: No document element found");
}

TEST(GraphmlParseTest, DeepNestingDoesNotCrash) {
    std::string nested;
    for (int i = 0; i < 100'000; ++i) {
        nested += "<x>";
    }
    for (int i = 0; i < 100'000; ++i) {
        nested += "</x>";
    }
    const auto witness =
        parseGraphmlWitness(graphml(entryKey, edgeGraph("<data key=\"assumption\">" + nested + "</data>")));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->edges.at(0).assumption, "");
}

TEST(GraphmlParseTest, RootMustBeGraphmlWithAGraph) {
    EXPECT_EQ(errorOf("<?xml version=\"1.0\"?><graph/>"), "the root element is <graph>, not <graphml>");
    EXPECT_EQ(errorOf("<graphml><key id=\"entry\"/></graphml>"), "the <graphml> element has no <graph>");
}

TEST(GraphmlParseTest, DuplicateNodeIdIsAnError) {
    EXPECT_EQ(errorOf(graphml(entryKey, "<node id=\"A\"><data key=\"entry\">true</data></node><node id=\"A\"/>")),
              "duplicate node id 'A'");
}

TEST(GraphmlParseTest, NodeWithoutIdIsAnError) {
    EXPECT_EQ(errorOf(graphml(entryKey, "<node id=\"A\"><data key=\"entry\">true</data></node><node/>")),
              "node 2: missing attribute 'id'");
}

TEST(GraphmlParseTest, EdgeToAnUnknownNodeIsAnError) {
    const std::string nodes = "<node id=\"A\"><data key=\"entry\">true</data></node><node id=\"B\"/>";
    EXPECT_EQ(errorOf(graphml(entryKey, nodes + "<edge source=\"A\" target=\"C\"/>")),
              "edge 1 ('A' -> 'C'): unknown target node");
    EXPECT_EQ(errorOf(graphml(entryKey, nodes + "<edge source=\"A\" target=\"B\"/><edge source=\"X\" target=\"B\"/>")),
              "edge 2 ('X' -> 'B'): unknown source node");
    EXPECT_EQ(errorOf(graphml(entryKey, nodes + "<edge target=\"B\"/>")),
              "edge 1 ('' -> 'B'): missing attribute 'source'");
}

TEST(GraphmlParseTest, EdgesMayPrecedeTheirNodes) {
    const auto witness = parseGraphmlWitness(
        graphml(entryKey, "<edge source=\"A\" target=\"B\"/><node id=\"A\"><data key=\"entry\">true</data></node>"
                          "<node id=\"B\"/>"));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->edges.size(), 1u);
}

TEST(GraphmlParseTest, ExactlyOneEntryNodeIsRequired) {
    EXPECT_EQ(errorOf(graphml(entryKey, "<node id=\"A\"/><node id=\"B\"/>")),
              "expected exactly one entry node, found 0");
    EXPECT_EQ(errorOf(graphml(entryKey, "<node id=\"A\"><data key=\"entry\">true</data></node>"
                                        "<node id=\"B\"><data key=\"entry\">true</data></node>")),
              "expected exactly one entry node, found 2");
}

TEST(GraphmlParseTest, BooleansMustBeTrueOrFalse) {
    EXPECT_EQ(errorOf(graphml(entryKey, "<node id=\"A\"><data key=\"entry\">yes</data></node>")),
              "node 'A': isEntryNode must be true or false, not 'yes'");
    EXPECT_EQ(errorOf(graphml(entryKey, edgeGraph("<data key=\"enterLoopHead\">1</data>"))),
              "edge 1 ('A' -> 'B'): enterLoopHead must be true or false, not '1'");
}

TEST(GraphmlParseTest, NumbersMustBeNonNegativeIntegers) {
    for (const char* key : {"startline", "endline", "startoffset", "endoffset"}) {
        EXPECT_EQ(errorOf(graphml(entryKey, edgeGraph(std::format("<data key=\"{}\">-1</data>", key)))),
                  std::format("edge 1 ('A' -> 'B'): {} must be a non-negative integer, not '-1'", key));
    }
    EXPECT_THAT(errorOf(graphml(entryKey, edgeGraph("<data key=\"startline\">twelve</data>"))),
                HasSubstr("startline must be a non-negative integer, not 'twelve'"));
}

} // namespace
} // namespace nitwit::witness
