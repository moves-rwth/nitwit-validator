//
// Parsing of violation witnesses in the YAML based witness format 2.x, see
// https://gitlab.com/sosy-lab/benchmarking/sv-witnesses/-/blob/main/user-guide/Witness-Format.md
//

#ifndef NITWIT_WITNESS_YAML_HPP
#define NITWIT_WITNESS_YAML_HPP

#include <cstddef>
#include <string>
#include <vector>

enum class WaypointType {
	Assumption,
	Branching,
	FunctionEnter,
	FunctionReturn,
	Target
};

enum class WaypointAction {
	Follow,
	Avoid,
	Cycle
};

// The column of a location is not read, as NITWIT only matches lines.
struct WaypointLocation {
	std::string file_name;
	std::size_t line = 0;
	std::string function;
};

struct Waypoint {
	WaypointType type = WaypointType::Assumption;
	WaypointAction action = WaypointAction::Follow;
	WaypointLocation location;
	bool has_constraint = false;
	std::string constraint_value;
	std::size_t thread_id = 0;
};

struct Segment {
	std::vector<Waypoint> waypoints;
};

// The metadata of the entry is not read, as NITWIT does not use it.
struct ViolationSequence {
	std::vector<Segment> segments;
};

struct YamlWitness {
	bool has_violation_sequence = false;
	ViolationSequence violation_sequence;
};

// Returns false (and prints the reason) if the file is not a well-formed YAML witness.
bool parseYamlWitness(std::string const& filename, YamlWitness& witness);

// Returns an empty string if NITWIT can validate the violation sequence, otherwise the reason why it can not.
std::string checkViolationSequenceSupported(ViolationSequence const& sequence);

#endif //NITWIT_WITNESS_YAML_HPP
