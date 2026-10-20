//
// Parsing of violation witnesses in the YAML based witness format 2.x, see
// https://gitlab.com/sosy-lab/benchmarking/sv-witnesses/-/blob/main/user-guide/Witness-Format.md
//

#include "witness_yaml.hpp"

#include <yaml-cpp/yaml.h>

#include <iostream>
#include <stdexcept>

namespace {
	YAML::Node requireMap(YAML::Node const& node, std::string const& context) {
		if (!node.IsMap()) {
			throw std::runtime_error(context + " is not a mapping");
		}
		return node;
	}

	YAML::Node requireSequence(YAML::Node const& node, std::string const& context) {
		if (!node.IsSequence()) {
			throw std::runtime_error(context + " is not a list");
		}
		return node;
	}

	// Missing keys and keys with a null value are treated the same.
	bool hasKey(YAML::Node const& map, char const* key) {
		YAML::Node const child = map[key];
		return child && !child.IsNull();
	}

	YAML::Node getRequired(YAML::Node const& map, char const* key, std::string const& context) {
		if (!hasKey(map, key)) {
			throw std::runtime_error(std::string("missing key '") + key + "' in " + context);
		}
		return map[key];
	}

	// Scalars keep their textual representation, e.g. the unquoted boolean "value: true" yields "true".
	std::string scalarToString(YAML::Node const& node, std::string const& context) {
		if (!node.IsScalar()) {
			throw std::runtime_error(context + " is not a scalar");
		}
		return node.Scalar();
	}

	std::string getRequiredString(YAML::Node const& map, char const* key, std::string const& context) {
		return scalarToString(getRequired(map, key, context), context + "." + key);
	}

	std::string getOptionalString(YAML::Node const& map, char const* key, std::string const& context) {
		if (!hasKey(map, key)) {
			return "";
		}
		return scalarToString(map[key], context + "." + key);
	}

	void replaceAll(std::string& s, std::string const& from, std::string const& to) {
		for (std::size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) {
			s.replace(pos, from.size(), to);
		}
	}

	// Some producers write "\result" or "\at" into double quoted YAML strings, where "\r" and "\a" are escape
	// sequences for a carriage return and a bell, so a correct YAML parser returns "<CR>esult" and "<BEL>t".
	std::string repairEscapes(std::string value) {
		replaceAll(value, "\result", "\\result");
		replaceAll(value, "\at(", "\\at(");
		return value;
	}

	WaypointType parseWaypointType(std::string const& type, std::string const& context) {
		if (type == "assumption") {
			return WaypointType::Assumption;
		} else if (type == "branching") {
			return WaypointType::Branching;
		} else if (type == "function_enter") {
			return WaypointType::FunctionEnter;
		} else if (type == "function_return") {
			return WaypointType::FunctionReturn;
		} else if (type == "target") {
			return WaypointType::Target;
		}
		throw std::runtime_error("unknown waypoint type '" + type + "' in " + context);
	}

	WaypointAction parseWaypointAction(std::string const& action, std::string const& context) {
		if (action == "follow") {
			return WaypointAction::Follow;
		} else if (action == "avoid") {
			return WaypointAction::Avoid;
		} else if (action == "cycle") {
			return WaypointAction::Cycle;
		}
		throw std::runtime_error("unknown waypoint action '" + action + "' in " + context);
	}

	Waypoint parseWaypoint(YAML::Node const& node, std::string const& context) {
		requireMap(node, context);
		Waypoint w;
		w.type = parseWaypointType(getRequiredString(node, "type", context), context);
		w.action = parseWaypointAction(getRequiredString(node, "action", context), context);

		std::string const locationContext = context + ".location";
		YAML::Node const location = requireMap(getRequired(node, "location", context), locationContext);
		w.location.file_name = getOptionalString(location, "file_name", locationContext);
		w.location.line = getRequired(location, "line", locationContext).as<std::size_t>();
		if (w.location.line == 0) {
			throw std::runtime_error(locationContext + ".line must be at least 1");
		}
		if (hasKey(location, "column")) {
			w.location.column = location["column"].as<std::size_t>();
		}
		w.location.function = getOptionalString(location, "function", locationContext);

		if (hasKey(node, "constraint")) {
			std::string const constraintContext = context + ".constraint";
			YAML::Node const constraint = requireMap(node["constraint"], constraintContext);
			w.has_constraint = true;
			w.constraint_value = repairEscapes(getRequiredString(constraint, "value", constraintContext));
		}

		if (hasKey(node, "thread_id")) {
			w.thread_id = node["thread_id"].as<std::size_t>();
		}
		return w;
	}

	void parseViolationSequence(YAML::Node const& entry, ViolationSequence& sequence) {
		YAML::Node const content = requireSequence(getRequired(entry, "content", "violation_sequence entry"), "content");
		for (auto const& segmentItem: content) {
			std::string const segmentContext = "segment " + std::to_string(sequence.segments.size() + 1);
			requireMap(segmentItem, segmentContext);
			Segment segment;
			for (auto const& waypointItem: requireSequence(getRequired(segmentItem, "segment", segmentContext), segmentContext)) {
				std::string const waypointContext = segmentContext + ", waypoint " + std::to_string(segment.waypoints.size() + 1);
				requireMap(waypointItem, waypointContext);
				segment.waypoints.push_back(parseWaypoint(getRequired(waypointItem, "waypoint", waypointContext), waypointContext));
			}
			sequence.segments.push_back(segment);
		}
	}
}

bool parseYamlWitness(std::string const& filename, YamlWitness& witness) {
	if (filename.empty()) {
		std::cerr << "No witness file name provided." << std::endl;
		return false;
	}

	try {
		YAML::Node const root = YAML::LoadFile(filename);
		if (!root || root.IsNull()) {
			std::cerr << "Witness file is empty." << std::endl;
			return false;
		}
		requireSequence(root, "The witness");

		std::size_t entryNumber = 0;
		for (auto const& entry: root) {
			std::string const entryContext = "entry " + std::to_string(++entryNumber);
			requireMap(entry, entryContext);
			if (getRequiredString(entry, "entry_type", entryContext) != "violation_sequence") {
				continue;
			}
			if (witness.has_violation_sequence) {
				throw std::runtime_error("a violation witness must contain exactly one violation_sequence entry");
			}
			parseViolationSequence(entry, witness.violation_sequence);
			witness.has_violation_sequence = true;
		}
	} catch (std::runtime_error const& e) {
		// also catches the exceptions of yaml-cpp, which derive from std::runtime_error
		std::cerr << "Failed to parse witness file. Reason: " << e.what() << std::endl;
		return false;
	}

	return true;
}

std::string checkViolationSequenceSupported(ViolationSequence const& sequence) {
	for (std::size_t i = 0; i < sequence.segments.size(); ++i) {
		std::size_t followTargets = 0;
		for (auto const& w: sequence.segments[i].waypoints) {
			if (w.action == WaypointAction::Cycle) {
				return "cycle waypoints (non-termination) in segment " + std::to_string(i + 1);
			}
			if (w.thread_id != 0) {
				return "waypoints of threads other than the main thread in segment " + std::to_string(i + 1);
			}
			if (w.action == WaypointAction::Follow && w.type == WaypointType::Target) {
				++followTargets;
			}
		}
		if (followTargets > 1) {
			return "multiple target waypoints (data race) in segment " + std::to_string(i + 1);
		}
	}
	return "";
}
