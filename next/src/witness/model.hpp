#pragma once

// In-memory models of violation witnesses: format 2.x (YAML, https://gitlab.com/sosy-lab/benchmarking/sv-witnesses)
// and format 1.0 (GraphML). The models are plain data close to the file formats; they know nothing about C.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nitwit::witness {

// ---- Format 2.x ----

enum class WaypointType : std::uint8_t { Assumption, Branching, FunctionEnter, FunctionReturn, Target };
enum class WaypointAction : std::uint8_t { Follow, Avoid, Cycle };

struct Location {
    std::string fileName;
    std::uint32_t line = 0;              // 1-based
    std::optional<std::uint32_t> column; // 1-based; not given in the witness if empty
    std::string function;                // empty if not given
};

struct Constraint {
    /// For `assumption` and `function_return`: a C expression; for `branching`: "true" or "false", or for a `switch`
    /// the value of the case label (or "default"). Strings that YAML escape processing mangled are repaired, see
    /// parseYamlWitness.
    std::string value;
    std::string format = "c_expression"; // or "acsl"
};

struct Waypoint {
    WaypointType type = WaypointType::Assumption;
    WaypointAction action = WaypointAction::Follow;
    Location location;
    std::optional<Constraint> constraint;
    std::optional<std::uint32_t> threadId; // format 2.1, concurrent programs
};

struct Segment {
    std::vector<Waypoint> waypoints;
};

struct Producer {
    std::string name;
    std::string version;
};

struct Task {
    std::vector<std::string> inputFiles;
    std::string specification; // the property, e.g. "G ! call(reach_error())"
    std::string dataModel;     // "ILP32" or "LP64"
    std::string language;      // "C"
};

struct Metadata {
    std::string formatVersion; // "2.0" or "2.1"
    Producer producer;
    Task task;
};

/// The entry of type `violation_sequence`. Other entry types (e.g. `invariant_set`) are not modelled.
struct ViolationSequence {
    Metadata metadata;
    std::vector<Segment> segments;
};

struct YamlWitness {
    /// All `violation_sequence` entries in file order; SV-COMP witnesses have exactly one.
    std::vector<ViolationSequence> violationSequences;
    /// The types of the other entries, e.g. "invariant_set", for messages.
    std::vector<std::string> otherEntryTypes;
};

// ---- Format 1.0 ----

struct GraphmlNode {
    std::string id;
    bool isEntry = false;
    bool isViolation = false;
    bool isSink = false;
};

/// An edge with the data keys of format 1.0 that NITWIT uses; unknown keys are ignored.
struct GraphmlEdge {
    std::string source;
    std::string target;
    std::optional<std::uint32_t> startLine;
    std::optional<std::uint32_t> endLine;
    std::optional<std::uint32_t> startOffset;
    std::optional<std::uint32_t> endOffset;
    std::optional<std::string> assumption;      // C expressions separated by ';'
    std::optional<std::string> assumptionScope; // function name
    std::optional<std::string> assumptionResultFunction;
    std::optional<std::string> control; // "condition-true" or "condition-false"
    std::optional<std::string> enterFunction;
    std::optional<std::string> returnFromFunction;
    std::optional<std::string> originFileName;
    bool enterLoopHead = false;
    std::optional<std::string> threadId;
};

struct GraphmlWitness {
    std::string witnessType; // "violation_witness" or "correctness_witness"
    std::string sourceCodeLang;
    std::string producer;
    std::string specification;
    std::string programFile;
    std::string architecture; // "32bit" or "64bit"
    std::vector<GraphmlNode> nodes;
    std::vector<GraphmlEdge> edges;
};

} // namespace nitwit::witness
