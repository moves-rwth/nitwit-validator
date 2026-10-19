//
// Construction of the witness automaton from a violation witness in the YAML based witness format 2.x.
//
// The violation sequence is translated into an automaton with one state per segment: the follow waypoint of segment i
// becomes an edge from state i to state i + 1, its avoid waypoints become edges from state i to the sink state (except
// those of type assumption and function_return, see below). The target waypoint of the last segment leads to the
// violation state.
//

#include "automaton.hpp"
#include "witness_yaml.hpp"

#include <cctype>
#include <exception>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace {
	// The automaton runner identifies the sink state by this id.
	std::string const SINK_NODE_ID = "sink";

	std::string trim(std::string const& s) {
		std::size_t const begin = s.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) {
			return "";
		}
		std::size_t const end = s.find_last_not_of(" \t\r\n");
		return s.substr(begin, end - begin + 1);
	}

	// Calls f(index) for every character of the expression that is not part of a string or character literal. Returns
	// false as soon as f returns false, or if a literal is not terminated.
	template<typename F>
	bool forEachCodeCharacter(std::string const& expression, F f) {
		char quote = '\0';
		for (std::size_t i = 0; i < expression.size(); ++i) {
			char const c = expression[i];
			if (quote != '\0') {
				if (c == '\\') {
					++i;
				} else if (c == quote) {
					quote = '\0';
				}
			} else if (c == '"' || c == '\'') {
				quote = c;
			} else if (!f(i)) {
				return false;
			}
		}
		return quote == '\0';
	}

	// Removes parentheses enclosing the whole expression, e.g. "((x == 1))" becomes "x == 1".
	std::string stripEnclosingParentheses(std::string expression) {
		expression = trim(expression);
		while (expression.size() >= 2 && expression.front() == '(' && expression.back() == ')') {
			// stops as soon as the first parenthesis is closed before the end of the expression
			int depth = 0;
			bool const enclosesAll = forEachCodeCharacter(expression, [&](std::size_t i) {
				if (expression[i] == '(') {
					++depth;
				} else if (expression[i] == ')') {
					--depth;
				}
				return depth > 0 || i + 1 == expression.size();
			});
			if (!enclosesAll || depth != 0) {
				break;
			}
			expression = trim(expression.substr(1, expression.size() - 2));
		}
		return expression;
	}

	// Splits an expression at its top level "&&" operators, e.g. "(x == 1) && (y == 2)" yields "x == 1" and "y == 2".
	std::vector<std::string> splitConjunction(std::string const& expression) {
		std::vector<std::string> conjuncts;
		std::string const e = stripEnclosingParentheses(expression);
		int depth = 0;
		std::size_t begin = 0;
		bool const balanced = forEachCodeCharacter(e, [&](std::size_t i) {
			char const c = e[i];
			if (c == '(' || c == '[' || c == '{') {
				++depth;
			} else if (c == ')' || c == ']' || c == '}') {
				--depth;
			} else if (depth == 0 && c == '&' && i + 1 < e.size() && e[i + 1] == '&') {
				conjuncts.push_back(stripEnclosingParentheses(e.substr(begin, i - begin)));
				begin = i + 2;
			}
			return depth >= 0;
		});
		if (!balanced || depth != 0) {
			// Leave anything we do not understand to the expression parser.
			return {e};
		}
		conjuncts.push_back(stripEnclosingParentheses(e.substr(begin)));
		return conjuncts;
	}

	// Joins the conjuncts in the form NITWIT uses for assumptions of GraphML witnesses, e.g. "x == 1; y == 2;".
	std::string toAssumption(std::vector<std::string> const& conjuncts) {
		std::string assumption;
		for (auto const& conjunct: conjuncts) {
			if (conjunct.empty()) {
				continue;
			}
			assumption += conjunct + "; ";
		}
		return trim(assumption);
	}

	// Adds delta (+1 / -1) to the integer literal "[-]magnitude".
	void adjustInteger(bool& negative, unsigned long long& magnitude, int delta) {
		bool const awayFromZero = (delta > 0) != negative;
		if (awayFromZero) {
			++magnitude;
		} else if (magnitude == 0) {
			negative = (delta < 0);
			magnitude = 1;
		} else {
			--magnitude;
			if (magnitude == 0) {
				negative = false;
			}
		}
	}

	// NITWIT resolves the value returned by a __VERIFIER_nondet_X() call from an assumption "\result == <constant>",
	// taking the first constant found after "\result". The constraint of a function_return waypoint may also be of
	// the form "\result <op> <constant>" with op in ==, !=, <=, <, >, >= (format 2.0), so for integer constants the
	// relation is replaced by an equality with a value satisfying it.
	std::string normalizeResultConstraint(std::string const& conjunct) {
		static std::regex const resultFirst(R"(^\\result\s*(==|!=|<=|>=|<|>)\s*(.+)$)");
		static std::regex const resultLast(R"(^(.+?)\s*(==|!=|<=|>=|<|>)\s*\\result$)");
		static std::regex const integerLiteral(R"(^(-?)\s*(0[xX][0-9a-fA-F]+|[0-9]+)([uUlL]*)$)");

		std::smatch match;
		std::string op;
		std::string constant;
		if (std::regex_match(conjunct, match, resultFirst)) {
			op = match[1];
			constant = trim(match[2]);
		} else if (std::regex_match(conjunct, match, resultLast)) {
			// mirror the relation, e.g. "5 < \result" is "\result > 5"
			static std::map<std::string, std::string> const mirrored = {{"==", "=="}, {"!=", "!="}, {"<=", ">="}, {">=", "<="}, {"<", ">"}, {">", "<"}};
			op = mirrored.at(match[2]);
			constant = trim(match[1]);
		} else {
			return conjunct;
		}

		std::string const stripped = stripEnclosingParentheses(constant);
		std::smatch literal;
		if (op == "==" || !std::regex_match(stripped, literal, integerLiteral)) {
			return "\\result " + op + " " + constant;
		}

		bool negative = literal[1].length() > 0;
		unsigned long long magnitude = 0;
		try {
			magnitude = std::stoull(literal[2].str(), nullptr, 0);
		} catch (std::exception const&) {
			return "\\result " + op + " " + constant;
		}
		std::string const suffix = literal[3].str();
		if (op == "!=") {
			// 0 and 1 fit into every type
			negative = false;
			magnitude = (magnitude == 0) ? 1 : 0;
		} else if (op == ">") {
			adjustInteger(negative, magnitude, 1);
		} else if (op == "<") {
			adjustInteger(negative, magnitude, -1);
		}
		// "<=" and ">=" are satisfied by the constant itself
		return "\\result == " + std::string(negative ? "-" : "") + std::to_string(magnitude) + suffix;
	}

	std::string toLower(std::string s) {
		for (auto& c: s) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return s;
	}

	std::shared_ptr<Edge> edgeFromWaypoint(Waypoint const& waypoint, std::string const& sourceId, std::string const& targetId) {
		auto e = std::make_shared<Edge>();
		e->source_id = sourceId;
		e->target_id = targetId;
		e->origin_file = waypoint.location.file_name;
		e->start_line = waypoint.location.line;
		e->end_line = waypoint.location.line;
		e->start_offset = 0;
		e->end_offset = 0;
		e->controlCondition = ConditionUndefined;
		e->enterLoopHead = false;

		switch (waypoint.type) {
			case WaypointType::Assumption:
				if (waypoint.has_constraint) {
					e->assumption = toAssumption(splitConjunction(waypoint.constraint_value));
					e->assumption_scope = waypoint.location.function;
				}
				break;
			case WaypointType::Branching: {
				std::string const value = toLower(trim(waypoint.constraint_value));
				if (value == "true") {
					e->control = "condition-true";
					e->controlCondition = ConditionTrue;
				} else if (value == "false") {
					e->control = "condition-false";
					e->controlCondition = ConditionFalse;
				} else {
					// A switch statement (value is a case constant or "default"): NITWIT does not report which case
					// was taken, so the edge only requires reaching the line.
#ifdef VERBOSE
					std::cerr << " ### Branching waypoint with value '" << waypoint.constraint_value << "' at line " << waypoint.location.line << " is only matched by its line." << std::endl;
#endif
				}
				break;
			}
			case WaypointType::FunctionEnter:
				e->enter_any_function = true;
				break;
			case WaypointType::FunctionReturn:
				e->return_from_any_function = true;
				if (waypoint.has_constraint) {
					std::vector<std::string> conjuncts = splitConjunction(waypoint.constraint_value);
					for (auto& conjunct: conjuncts) {
						conjunct = normalizeResultConstraint(conjunct);
					}
					e->assumption = toAssumption(conjuncts);
					e->assumption_scope = waypoint.location.function;
				}
				break;
			case WaypointType::Target:
				break;
		}
		return e;
	}

	std::shared_ptr<Node> makeNode(std::string const& id) {
		auto n = std::make_shared<Node>();
		n->id = id;
		return n;
	}

	std::string stateId(std::size_t segmentIndex) {
		return "S" + std::to_string(segmentIndex);
	}
}

std::shared_ptr<WitnessAutomaton> WitnessAutomaton::automatonFromYamlWitness(ViolationSequence const& sequence) {
	std::map<std::string, std::shared_ptr<Node>> nodes;
	std::vector<std::shared_ptr<Edge>> edges;

	std::size_t const segmentCount = sequence.segments.size();
	if (segmentCount == 0) {
		std::cerr << " ### The violation sequence contains no segments." << std::endl;
		return nullptr;
	}

	for (std::size_t i = 0; i <= segmentCount; ++i) {
		auto n = makeNode(stateId(i));
		n->is_entry = (i == 0);
		n->is_violation = (i == segmentCount);
		nodes.emplace(n->id, n);
	}
	auto sink = makeNode(SINK_NODE_ID);
	sink->is_sink = true;
	nodes.emplace(sink->id, sink);

	for (std::size_t i = 0; i < segmentCount; ++i) {
		bool const isLastSegment = (i + 1 == segmentCount);
		Waypoint const* follow = nullptr;
		for (auto const& waypoint: sequence.segments[i].waypoints) {
			if (waypoint.action == WaypointAction::Follow) {
				if (follow != nullptr) {
					std::cerr << " ### Segment " << (i + 1) << " contains more than one follow waypoint." << std::endl;
					return nullptr;
				}
				follow = &waypoint;
				continue;
			}

			// avoid waypoint (cycle waypoints were rejected by checkViolationSequenceSupported)
			if (waypoint.type == WaypointType::Target) {
				std::cerr << " ### Segment " << (i + 1) << " contains a target waypoint with action avoid." << std::endl;
				return nullptr;
			}
			if (waypoint.type == WaypointType::Assumption || waypoint.type == WaypointType::FunctionReturn) {
				// Checking a constraint resolves nondeterministic values, which would turn into a side effect here.
#ifdef VERBOSE
				std::cerr << " ### Ignoring avoid waypoint with a constraint in segment " << (i + 1) << " at line " << waypoint.location.line << "." << std::endl;
#endif
				continue;
			}
			edges.push_back(edgeFromWaypoint(waypoint, stateId(i), SINK_NODE_ID));
		}

		if (follow == nullptr) {
			std::cerr << " ### Segment " << (i + 1) << " contains no follow waypoint." << std::endl;
			return nullptr;
		}
		if ((follow->type == WaypointType::Target) != isLastSegment) {
			std::cerr << " ### Exactly the last segment has to end in a target waypoint, but segment " << (i + 1) << (isLastSegment ? " does not." : " does.") << std::endl;
			return nullptr;
		}
		edges.push_back(edgeFromWaypoint(*follow, stateId(i), stateId(i + 1)));
	}

	auto data = std::make_shared<Data>();
	data->witness_type = "violation_witness";
	data->is_yaml_format = true;
	return std::make_shared<WitnessAutomaton>(nodes, edges, data);
}
