//
// Created by jan svejda on 3.4.19.
//

#ifndef NITWIT_AUTOMATON_HPP
#define NITWIT_AUTOMATON_HPP

#include "../picoc/picoc.hpp"

#include <iostream>

#undef min

#include <cstddef>
#include "../utils/pugixml/pugixml.hpp"
#include <string>
#include <deque>
#include <utility>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <algorithm>
#include <iostream>

#include "../picoc/verbose.hpp"

class Node;

class Edge;

class WitnessAutomaton;

class DefaultKeyValues;

struct Data;

struct ViolationSequence;

class ProgramState;

class Node {
public:
	std::string id;
	std::string node_type;
	std::string invariant;
	std::string invariant_scope;
	bool is_entry{};
	bool is_violation{};
	bool is_sink{};
	bool is_frontier{};
	bool is_loopHead{};
	std::size_t thread_number{};

	void print() const;
};


class Edge {
public:
	std::string source_id;
	std::string target_id;
	std::string origin_file;
	std::string assumption;
	std::string assumption_scope;
	std::string assumption_result_function;
	std::string enter_function;
	std::string return_from_function;
	std::string source_code;
	std::string control;
	ConditionControl controlCondition;
	std::size_t start_line;
	std::size_t end_line;
	std::size_t start_offset;
	std::size_t end_offset;
	bool enterLoopHead;
	// Witness format 2.x: function_enter / function_return waypoints do not name the function, they match the entry
	// into / the return from any function called at their location
	bool enter_any_function{};
	bool return_from_any_function{};
	// Witness format 2.x: a waypoint is evaluated at a specific check point of the interpreter, at its column (0 if
	// the column is not given). A follow waypoint has to pass the first time its evaluation point is reached.
	bool requires_check_point{};
	DebugCheckPoint check_point{};
	std::size_t column{};
	bool strict{};
	// Witness format 2.x: for an avoid function_return waypoint, the assumption "\result == <constant>" that chooses a
	// value returned by a __VERIFIER_nondet_X() function not passing the waypoint
	std::string avoid_resolution;

	void print() const;
};

struct Key {
	std::string name;
	std::string type;
	std::string for_;
	std::string id;
	std::string default_val;

	Key() : name(), type(), for_(), id(), default_val() {}

	Key(std::string&& name, std::string&& type, std::string&& for_, std::string&& id, std::string&& defaultVal) :
			name(std::move(name)), type(std::move(type)), for_(std::move(for_)),
			id(std::move(id)), default_val(std::move(defaultVal)) {}

	static void printKey(Key *k) {
		std::cout << "name: " << k->name;
		std::cout << "\t" << "type: " << k->type;
		std::cout << "\t" << "for: " << k->for_;
		std::cout << "\t" << "id: " << k->id;
		std::cout << "\t" << "def: " << k->default_val << std::endl;
	}
};

class DefaultKeyValues {
	std::map<std::string, Key> default_keys;

public:
	void addKey(Key const& k);

	Key getDefault(std::string const& id) const;

	void print() const;
};

struct Data {
	std::string source_code_lang;
	std::string program_file;
	std::string program_hash;
	std::string specification;
	std::string architecture;
	std::string producer;
	std::string witness_type;
	// witness in the YAML based format 2.x (as opposed to GraphML, format 1.0)
	bool is_yaml_format{};

	void print() const;
};

class WitnessAutomaton {
private:
	std::map<std::string, std::shared_ptr<Node>> nodes;
	std::vector<std::shared_ptr<Edge>> edges;
	Data data;
	std::shared_ptr<Node> current_state;

	std::map<std::string, std::set<std::shared_ptr<Edge>>> successor_rel;
	std::map<std::string, std::set<std::shared_ptr<Edge>>> predecessor_rel;
	bool illegal_state = false;
	bool verifier_error_called = false;
	std::size_t unsuccessfulTries = 0;

public:
	WitnessAutomaton(std::map<std::string, std::shared_ptr<Node>> const& nodes, std::vector<std::shared_ptr<Edge>> const& edges, std::shared_ptr<Data>& data);

	WitnessAutomaton();

	Data const& getData() const;

	void printData() const;

	void printRelations() const;

	bool consumeState(ParseState *state, bool isMultiLineDeclaration, std::size_t const& endLine, bool isInitialCheck);

	bool isInIllegalState() const;

	static std::shared_ptr<WitnessAutomaton> automatonFromWitness(std::shared_ptr<pugi::xml_document> const& doc);

	// Returns nullptr if the violation sequence is invalid. The program source is needed for the values of switch
	// statements.
	static std::shared_ptr<WitnessAutomaton> automatonFromYamlWitness(ViolationSequence const& sequence, std::string const& programSource);

	bool isInViolationState() const;

	bool isInSinkState() const;

	bool wasVerifierErrorCalled() const;

	const std::shared_ptr<Node>& getCurrentState() const;

	bool canTransitionFurther();

	std::size_t getUnsuccessfulTries() const;
};


#endif //NITWIT_AUTOMATON_HPP
