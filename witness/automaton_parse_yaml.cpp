//
// Construction of the witness automaton from a violation witness in the YAML based witness format 2.x.
//
// The violation sequence is translated into an automaton with one state per segment: the follow waypoint of segment i
// becomes an edge from state i to state i + 1, its avoid waypoints become edges from state i to the sink state (except
// those whose constraint can not be checked). The target waypoint of the last segment leads to the violation state.
//
// Each edge is matched at the check point of the interpreter where its waypoint is evaluated (see DebugCheckPoint), at
// the line and column of the waypoint. A follow waypoint that does not pass the first time its evaluation point is
// reached leads to the sink state as well, as the witness then does not describe the execution.
//

#include "automaton.hpp"
#include "witness_yaml.hpp"

#include <algorithm>
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
	std::regex const resultFirst(R"(^\\result\s*(==|!=|<=|>=|<|>)\s*(.+)$)");
	std::regex const resultLast(R"(^(.+?)\s*(==|!=|<=|>=|<|>)\s*\\result$)");

	std::string normalizeResultConstraint(std::string const& conjunct) {
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

	// For an avoid function_return waypoint: the assumption "\result == <constant>" choosing a return value that does not
	// satisfy the constraint (the negation of its first relation of "\result" to an integer constant), or an empty
	// string if there is no such relation.
	std::string returnValueNotSatisfying(std::string const& constraint) {
		static std::map<std::string, std::string> const negated = {{"==", "!="}, {"!=", "=="}, {"<", ">="}, {">=", "<"}, {">", "<="}, {"<=", ">"}};
		for (auto const& conjunct: splitConjunction(constraint)) {
			std::smatch match;
			std::string negation;
			if (std::regex_match(conjunct, match, resultFirst)) {
				negation = "\\result " + negated.at(match[1]) + " " + trim(match[2]);
			} else if (std::regex_match(conjunct, match, resultLast)) {
				negation = trim(match[1]) + " " + negated.at(match[2]) + " \\result";
			} else {
				continue;
			}
			std::string const value = normalizeResultConstraint(negation);
			if (value.rfind("\\result == ", 0) == 0) {
				return value + ";";
			}
		}
		return "";
	}

	std::string toLower(std::string s) {
		for (auto& c: s) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return s;
	}

	bool isSwitchBranching(Waypoint const& waypoint) {
		std::string const value = toLower(trim(waypoint.constraint_value));
		return waypoint.type == WaypointType::Branching && value != "true" && value != "false";
	}

	// The controlling expression of the switch statement at the given position (the first one in the line if the
	// column is 0), or an empty string if there is none.
	std::string switchControllingExpression(std::vector<std::string> const& sourceLines, std::size_t line, std::size_t column) {
		if (line == 0 || line > sourceLines.size()) {
			return "";
		}
		// the controlling expression may continue in the following lines
		std::string text = sourceLines[line - 1].substr(column == 0 ? 0 : std::min(column - 1, sourceLines[line - 1].size()));
		for (std::size_t i = line; i < sourceLines.size() && i < line + 10; ++i) {
			text += " " + sourceLines[i];
		}
		static std::regex const switchStart(R"(^\s*switch\s*\()");
		std::smatch match;
		std::size_t const keyword = (column == 0) ? text.find("switch") : 0;
		if (keyword == std::string::npos) {
			return "";
		}
		std::string const fromKeyword = text.substr(keyword);
		if (!std::regex_search(fromKeyword, match, switchStart)) {
			return "";
		}
		std::string const rest = fromKeyword.substr(match.length());
		int depth = 1;
		std::size_t end = std::string::npos;
		forEachCodeCharacter(rest, [&](std::size_t i) {
			if (rest[i] == '(') {
				++depth;
			} else if (rest[i] == ')' && --depth == 0) {
				end = i;
				return false;
			}
			return true;
		});
		return (end == std::string::npos) ? "" : trim(rest.substr(0, end));
	}

	// The check point of the interpreter where a waypoint of the given type is evaluated, see DebugCheckPoint.
	DebugCheckPoint evaluationCheckPoint(Waypoint const& waypoint) {
		switch (waypoint.type) {
			case WaypointType::Branching:
				// the value of a switch statement is checked before the statement, see edgeFromWaypoint
				return isSwitchBranching(waypoint) ? DebugCheckPoint::BeforeStatement : DebugCheckPoint::Condition;
			case WaypointType::FunctionEnter:
				return DebugCheckPoint::AfterArguments;
			case WaypointType::FunctionReturn:
				return DebugCheckPoint::AfterCall;
			case WaypointType::Assumption:
			case WaypointType::Target:
				break;
		}
		return DebugCheckPoint::BeforeStatement;
	}

	std::shared_ptr<Edge> edgeFromWaypoint(Waypoint const& waypoint, std::string const& sourceId, std::string const& targetId, std::vector<std::string> const& sourceLines) {
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
		e->requires_check_point = true;
		e->check_point = evaluationCheckPoint(waypoint);
		e->column = waypoint.location.column;
		// target waypoints are never evaluated, avoid waypoints lead to the sink when they pass
		e->strict = (waypoint.action == WaypointAction::Follow && waypoint.type != WaypointType::Target);

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
				} else if (value != "default") {
					// The value of a switch statement is the case taken, it has to be the value of the controlling
					// expression. "default" (no case matches) can not be expressed this way.
					std::string const expression = switchControllingExpression(sourceLines, waypoint.location.line, waypoint.location.column);
					if (!expression.empty()) {
						static std::regex const identifier(R"(^[A-Za-z_]\w*$)");
						bool const plain = std::regex_match(expression, identifier);
						e->assumption = (plain ? expression : "(" + expression + ")") + " == " + trim(waypoint.constraint_value) + ";";
					}
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

std::shared_ptr<WitnessAutomaton> WitnessAutomaton::automatonFromYamlWitness(ViolationSequence const& sequence, std::string const& programSource) {
	std::map<std::string, std::shared_ptr<Node>> nodes;
	std::vector<std::shared_ptr<Edge>> edges;

	std::vector<std::string> sourceLines;
	for (std::size_t begin = 0; begin <= programSource.size();) {
		std::size_t const end = programSource.find('\n', begin);
		sourceLines.push_back(programSource.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
		begin = (end == std::string::npos) ? programSource.size() + 1 : end + 1;
	}

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
			auto avoid = edgeFromWaypoint(waypoint, stateId(i), SINK_NODE_ID, sourceLines);
			if (waypoint.type == WaypointType::FunctionReturn) {
				// the returned value is chosen not to pass the waypoint, see passesAvoidWaypoint
				avoid->assumption.clear();
				avoid->avoid_resolution = returnValueNotSatisfying(waypoint.constraint_value);
			}
			if ((waypoint.type == WaypointType::FunctionReturn && avoid->avoid_resolution.empty()) || (isSwitchBranching(waypoint) && avoid->assumption.empty())) {
				// e.g. "default" of a switch statement, which is not checked
#ifdef VERBOSE
				std::cerr << " ### Ignoring avoid waypoint in segment " << (i + 1) << " at line " << waypoint.location.line << ", its constraint can not be checked." << std::endl;
#endif
				continue;
			}
			edges.push_back(avoid);
		}

		if (follow == nullptr) {
			std::cerr << " ### Segment " << (i + 1) << " contains no follow waypoint." << std::endl;
			return nullptr;
		}
		if ((follow->type == WaypointType::Target) != isLastSegment) {
			std::cerr << " ### Exactly the last segment has to end in a target waypoint, but segment " << (i + 1) << (isLastSegment ? " does not." : " does.") << std::endl;
			return nullptr;
		}
		edges.push_back(edgeFromWaypoint(*follow, stateId(i), stateId(i + 1), sourceLines));
	}

	auto data = std::make_shared<Data>();
	data->witness_type = "violation_witness";
	data->is_yaml_format = true;
	return std::make_shared<WitnessAutomaton>(nodes, edges, data);
}
