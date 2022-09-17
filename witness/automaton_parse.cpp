
//
// Created by jan svejda on 3.4.19.
//

#include "automaton.hpp"

#include <cstdlib>
#include <string>

std::shared_ptr<DefaultKeyValues> parseKeys(pugi::xpath_node_set const& keyNodeSet);

std::shared_ptr<DefaultKeyValues> getDefaultKeys();

std::map<std::string, std::shared_ptr<Node>> parseNodes(pugi::xpath_node_set const& set, std::shared_ptr<DefaultKeyValues> const& defaultKeyValues);

void setNodeAttributes(std::shared_ptr<Node> const& node, char const *name, char const *value);

std::shared_ptr<Node> getDefaultNode(std::shared_ptr<DefaultKeyValues> const& def_values);

std::vector<std::shared_ptr<Edge>> parseEdges(pugi::xpath_node_set const& set, std::map<std::string, std::shared_ptr<Node>> const& graphNodes, std::shared_ptr<DefaultKeyValues> const& defaultKeyValues);

std::shared_ptr<Data> parseData(const pugi::xpath_node_set& set);

std::shared_ptr<WitnessAutomaton> WitnessAutomaton::automatonFromWitness(std::shared_ptr<pugi::xml_document> const& doc) {
	pugi::xml_node root = doc->root().first_child();
	if (!root || strcmp(root.name(), "graphml") != 0) {
		std::cerr << " ### No graphml root element." << std::endl;
		return std::make_shared<WitnessAutomaton>();
	}
	std::string xpath = "/graphml/key"; // TODO add attributes
	auto key_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));

	std::shared_ptr<DefaultKeyValues> default_key_values;
	if (key_result.empty()) {
		std::cerr << " ### No graphml keys found!" << std::endl;
		default_key_values = getDefaultKeys();
	} else {
		default_key_values = parseKeys(key_result);
	}

	xpath = "/graphml/graph/node[@id]";
	auto node_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));
	if (node_result.empty()) {
		std::cerr << " ### There should be at least 1 node!" << std::endl;
		return std::make_shared<WitnessAutomaton>();
	}

	xpath = "/graphml/graph/edge[@source and @target]";
	auto edge_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));
	if (edge_result.empty()) {
		std::cerr << " ### There are no edges!" << std::endl;
		return std::make_shared<WitnessAutomaton>(); // TODO Could there be witnesses with no edges?
	}

	xpath = "/graphml/graph/data[@key]";
	auto graph_data_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));
	if (graph_data_result.empty()) {
		std::cerr << " ### There are no graph data!" << std::endl;
//        return make_shared<WitnessAutomaton>();
		throw;
	}

	auto nodes = parseNodes(node_result, default_key_values);
	// we need to check for loopHead node due to different graph syntaxes
	auto edges = parseEdges(edge_result, nodes, default_key_values);
	auto data = parseData(graph_data_result);
	auto aut = std::make_shared<WitnessAutomaton>(nodes, edges, data);

	return aut;
}

std::shared_ptr<Data> parseData(pugi::xpath_node_set const& set) {
	auto d = std::make_shared<Data>();
	for (auto xpath_node: set) {
		if (xpath_node.node() == nullptr) {
			continue;
		}
		auto attr = xpath_node.node().attribute("key");
		if (attr.empty()) {
			continue;
		}
		const char *name = attr.value();
		if (strcmp(name, "sourcecodelang") == 0) {
			d->source_code_lang = xpath_node.node().text().get();
		} else if (strcmp(name, "programfile") == 0) {
			d->program_file = xpath_node.node().text().get();
		} else if (strcmp(name, "programhash") == 0) {
			d->program_hash = xpath_node.node().text().get();
		} else if (strcmp(name, "specification") == 0) {
			d->specification = xpath_node.node().text().get();
		} else if (strcmp(name, "architecture") == 0) {
			d->architecture = xpath_node.node().text().get();
		} else if (strcmp(name, "producer") == 0) {
			d->producer = xpath_node.node().text().get();
		} else if (strcmp(name, "witness-type") == 0) {
			d->witness_type = xpath_node.node().text().get();
		}
	}
	return d;
}

std::size_t stringToSizeT(std::string const& s) {
	if (s.empty()) {
		// Special case, atoi does this as well.
		return 0;
	}
	try {
		if (sizeof(std::size_t) == sizeof(unsigned long)) {
			return static_cast<std::size_t>(std::stoul(s));
		} else if (sizeof(std::size_t) == sizeof(unsigned long long)) {
			return static_cast<std::size_t>(std::stoull(s));
		} else {
			// Unhandled size of size_t
			throw;
		}
	} catch (std::invalid_argument const& iae) {
		std::cerr << "Failed to convert string '" << s << "' to size_t!" << std::endl;
		return 0;
	}
}

std::size_t stringToSizeT(char const* s) {
	if ((s == nullptr) || (*s == '\0')) {
		// Special case, atoi does this as well.
		return 0;
	}
	try {
		if (sizeof(std::size_t) == sizeof(unsigned long)) {
			return static_cast<std::size_t>(std::strtoul(s, nullptr, 10));
		} else if (sizeof(std::size_t) == sizeof(unsigned long long)) {
			return static_cast<std::size_t>(std::strtoull(s, nullptr, 10));
		} else {
			// Unhandled size of size_t
			throw;
		}
	} catch (std::invalid_argument const& iae) {
		std::cerr << "Failed to convert string '" << s << "' to size_t!" << std::endl;
		return 0;
	}
}

std::shared_ptr<Edge> getDefaultEdge(std::shared_ptr<DefaultKeyValues> const& def_values) {
	auto e = std::make_shared<Edge>();

	// strings
	e->assumption = def_values->getDefault("assumption").default_val;
	e->assumption_scope = def_values->getDefault("assumption.scope").default_val;
	e->assumption_result_function = def_values->getDefault("assumption.resultfunction").default_val;
	e->origin_file = def_values->getDefault("originfile").default_val;
//    e.source_id = def_values->getDefault("nodetype").default_val;
//    e.target_id = def_values->getDefault("nodetype").default_val;
	e->control = def_values->getDefault("control").default_val;
	e->controlCondition = ConditionUndefined;
	e->enter_function = def_values->getDefault("enterFunction").default_val;
	e->return_from_function = def_values->getDefault("returnFrom").default_val;
	e->source_code = def_values->getDefault("sourcecode").default_val;

	// bools - default value for all is false, so only if default is "true", shall it be true
	e->enterLoopHead = (def_values->getDefault("enterLoopHead").default_val == "true");

	// integers
	e->start_line = stringToSizeT(def_values->getDefault("startline").default_val);
	e->start_line = stringToSizeT(def_values->getDefault("endline").default_val);
	e->start_offset = stringToSizeT(def_values->getDefault("startoffset").default_val);
	e->end_offset = stringToSizeT(def_values->getDefault("endoffset").default_val);

	return e;
}

std::string replace_equals(pugi::char_t const* value) {
	std::string s(value);
	s.replace(s.begin(), s.end(), "==", "= ");
	return s;
}

void setEdgeAttributes(std::shared_ptr<Edge>& edge, std::map<std::string, std::shared_ptr<Node>> const& nodes, pugi::char_t const* name, pugi::char_t const* value) {
	if (strcmp(name, "source") == 0) {
		edge->source_id = value;
	} else if (strcmp(name, "target") == 0) {
		edge->target_id = value;
		//check if we have an loopHead node
		if (nodes.find(value) != nodes.end() && nodes.find(value)->second->is_loopHead) {
			edge->enterLoopHead = true;
		}
	} else if (strcmp(name, "assumption") == 0) {
		edge->assumption = (value);
	} else if (strcmp(name, "assumption.scope") == 0) {
		edge->assumption_scope = value;
	} else if (strcmp(name, "assumption.resultfunction") == 0) {
		edge->assumption_result_function = value;
	} else if (strcmp(name, "originfile") == 0) {
		edge->origin_file = value;
	} else if (strcmp(name, "control") == 0) {
		edge->control = value;
		if (strcmp(value, "condition-true") == 0) {
			edge->controlCondition = ConditionTrue;
		} else if (strcmp(value, "condition-false") == 0) {
			edge->controlCondition = ConditionFalse;
		} else {
			edge->controlCondition = ConditionUndefined;
		}
	} else if (strcmp(name, "startoffset") == 0) {
		edge->start_offset = stringToSizeT(value);
	} else if (strcmp(name, "endoffset") == 0) {
		edge->end_offset = stringToSizeT(value);
	} else if (strcmp(name, "startline") == 0) {
		edge->start_line = stringToSizeT(value);
	} else if (strcmp(name, "endline") == 0) {
		edge->end_line = stringToSizeT(value);
	} else if (strcmp(name, "enterFunction") == 0) {
		edge->enter_function = value;
	} else if (strcmp(name, "returnFrom") == 0) {
		edge->return_from_function = value;
	} else if (strcmp(name, "sourcecode") == 0) {
		edge->source_code = value;
	} else if (strcmp(name, "enterLoopHead") == 0) {
		edge->enterLoopHead = strcmp(value, "true") == 0;
	} else if (strcmp(name, "threadId") != 0 && strcmp(name, "id") != 0) {
#ifdef VERBOSE
		std::cerr << " ### Unrecognized edge attribute definition: " << name << std::endl;
#endif
	}
}


void parseEdgeProperties(pugi::xml_node const& node, std::map<std::string, std::shared_ptr<Node>> const& nodes, std::shared_ptr<Edge>& edge) {
	for (auto child: node.children("data")) {
		auto key = child.attribute("key");
		if (!key.empty()) {
			setEdgeAttributes(edge, nodes, key.value(), child.text().get());
		}
	}
}

std::vector<std::shared_ptr<Edge>> parseEdges(pugi::xpath_node_set const& set, std::map<std::string, std::shared_ptr<Node>> const& graphNodes, std::shared_ptr<DefaultKeyValues> const& defaultKeyValues) {
	auto edges = std::vector<std::shared_ptr<Edge>>();
	edges.reserve(set.size());

	for (auto xpathNode: set) {
		if (!xpathNode.node()) {
			continue;
		}

		auto e = getDefaultEdge(defaultKeyValues);
		pugi::xml_node node = xpathNode.node();
		for (auto attr: node.attributes()) {
			setEdgeAttributes(e, graphNodes, attr.name(), attr.value());
		}
		parseEdgeProperties(node, graphNodes, e);

		edges.push_back(e);
	}

	return edges;
}

void parseNodeProperties(pugi::xml_node const& node, std::shared_ptr<Node> const& n) {
	for (auto child: node.children("data")) {
		auto key = child.attribute("key");
		if (!key.empty()) {
			setNodeAttributes(n, key.value(), child.text().get());
		}
	}
}

std::map<std::string, std::shared_ptr<Node>> parseNodes(pugi::xpath_node_set const& set, std::shared_ptr<DefaultKeyValues> const& defaultKeyValues) {
	auto nodes = std::map<std::string, std::shared_ptr<Node>>();

	for (auto xpathNode: set) {
		// check whether xpathNode is a node element
		if (!xpathNode.node()) {
			continue;
		}

		auto n = getDefaultNode(defaultKeyValues);
		pugi::xml_node node = xpathNode.node();
		for (auto attr: node.attributes()) {
			setNodeAttributes(n, attr.name(), attr.value());
		}
		parseNodeProperties(node, n);

		nodes.emplace(n->id, n);
	}

	return nodes;
}

std::shared_ptr<Node> getDefaultNode(std::shared_ptr<DefaultKeyValues> const& def_values) {
	auto n = std::make_shared<Node>();

	// strings
	n->invariant = def_values->getDefault("invariant").default_val;
	n->invariant_scope = def_values->getDefault("invariant.scope").default_val;
	n->node_type = def_values->getDefault("nodetype").default_val;

	// booleans - default value for all is false, so only if default is "true", shall it be true
	n->is_frontier = (def_values->getDefault("frontier").default_val == "true");
	n->is_violation = (def_values->getDefault("violation").default_val == "true");
	n->is_entry = (def_values->getDefault("entry").default_val == "true");
	n->is_sink = (def_values->getDefault("sink").default_val == "true");
	n->is_loopHead = (def_values->getDefault("loopHead").default_val == "true");

	// integers
	std::string val = def_values->getDefault("thread").default_val;
	n->thread_number = stringToSizeT(val);

	return n;
}

void setNodeAttributes(std::shared_ptr<Node> const& node, char const* name, char const* value) {
	if (strcmp(name, "id") == 0) {
		node->id = value;
	} else if (strcmp(name, "entry") == 0) {
		node->is_entry = (strcmp(value, "true") == 0);
	} else if (strcmp(name, "sink") == 0) {
		node->is_sink = (strcmp(value, "true") == 0);
	} else if (strcmp(name, "frontier") == 0) {
		node->is_frontier = (strcmp(value, "true") == 0);
	} else if (strcmp(name, "loopHead") == 0) {
		node->is_loopHead = (strcmp(value, "true") == 0);
	} else if (strcmp(name, "violation") == 0) {
		node->is_violation = (strcmp(value, "true") == 0);
	} else if (strcmp(name, "violatedProperty") == 0) {
		node->is_violation = true;
	} else if (strcmp(name, "invariant") == 0) {
		node->invariant = value;
	} else if (strcmp(name, "invariant.scope") == 0) {
		node->invariant_scope = value;
	} else if (strcmp(name, "nodetype") == 0) {
		node->node_type = value;
	} else if (strcmp(name, "thread") == 0) {
		node->thread_number = stringToSizeT(value);
	} else {
#ifdef VERBOSE
		std::cerr << " ### Unrecognized node attribute definition: " << name << std::endl;
#endif
	}
}

void setKeyAttributes(Key *key, const char *name, const char *value) {
	if (strcmp(name, "attr.name") == 0) {
		key->name = value;
	} else if (strcmp(name, "attr.type") == 0) {
		key->type = value;
	} else if (strcmp(name, "for") == 0) {
		key->for_ = value;
	} else if (strcmp(name, "id") == 0) {
		key->id = value;
	}
}

std::shared_ptr<DefaultKeyValues> parseKeys(pugi::xpath_node_set const& keyNodeSet) {
	auto dkv = std::make_unique<DefaultKeyValues>();

	for (pugi::xpath_node xpath_node: keyNodeSet) {
		if (!xpath_node.node()) {
			continue;
		}
		pugi::xml_node node = xpath_node.node();
		Key k;

		for (auto attrIt: node.attributes()) {
			setKeyAttributes(&k, attrIt.name(), attrIt.value());
		}

		pugi::xml_node cur = node.child("default");
		if (cur) {
			// has default value
			k.default_val = cur.text().get();
		}

		dkv->addKey(k);
	}

	return dkv;
}

std::shared_ptr<DefaultKeyValues> getDefaultKeys() {
	auto dkv = std::make_shared<DefaultKeyValues>();
	dkv->addKey(Key("violatedProperty", "string", "node", "violatedProperty", ""));
	dkv->addKey(Key("sourcecodeLanguage", "string", "graph", "sourcecodelang", ""));
	dkv->addKey(Key("programFile", "string", "graph", "programfile", ""));
	dkv->addKey(Key("programHash", "string", "graph", "programhash", ""));
	dkv->addKey(Key("specification", "string", "graph", "specification", ""));
	dkv->addKey(Key("architecture", "string", "graph", "architecture", ""));
	dkv->addKey(Key("producer", "string", "graph", "producer", ""));
	dkv->addKey(Key("creationTime", "string", "graph", "creationtime", ""));
	dkv->addKey(Key("startline", "int", "edge", "startline", ""));
	dkv->addKey(Key("endline", "int", "edge", "endline", ""));
	dkv->addKey(Key("startoffset", "int", "edge", "startoffset", ""));
	dkv->addKey(Key("endoffset", "int", "edge", "endoffset", ""));
	dkv->addKey(Key("control", "string", "edge", "control", ""));
	dkv->addKey(Key("assumption", "string", "edge", "assumption", ""));
	dkv->addKey(Key("assumption.scope", "string", "edge", "assumption.scope", ""));
	dkv->addKey(Key("enterFunction", "string", "edge", "enterFunction", ""));
	dkv->addKey(Key("returnFromFunction", "string", "edge", "returnFrom", ""));
	dkv->addKey(Key("witness-type", "string", "graph", "witness-type", ""));
	dkv->addKey(Key("inputWitnessHash", "string", "graph", "inputwitnesshash", ""));
	dkv->addKey(Key("originFileName", "string", "edge", "originfile", ""));
	dkv->addKey(Key("isViolationNode", "boolean", "node", "violation", "false"));
	dkv->addKey(Key("isEntryNode", "boolean", "node", "entry", "false"));
	dkv->addKey(Key("isSinkNode", "boolean", "node", "sink", "false"));
	dkv->addKey(Key("isLoopHeadNode", "boolean", "node", "loopHead", "false"));
	dkv->addKey(Key("enterLoopHead", "boolean", "edge", "enterLoopHead", "false"));
	return dkv;
}

void DefaultKeyValues::addKey(Key const& k) {
	this->default_keys.emplace(k.id, k);
}

Key DefaultKeyValues::getDefault(std::string const& id) const {
	auto it = this->default_keys.find(id);
	if (it == this->default_keys.end()) {
		return Key();
	}
	return it->second;
}

void DefaultKeyValues::print() const {
	for (auto item: this->default_keys) {
		Key::printKey(&item.second);
	}
}

WitnessAutomaton::WitnessAutomaton(std::map<std::string, std::shared_ptr<Node>> const& nodes, std::vector<std::shared_ptr<Edge>> const& edges, std::shared_ptr<Data>& data) : nodes((nodes)), edges((edges)), data(*data), current_state(nullptr), successor_rel(), predecessor_rel() {
	for (auto const& n: nodes) {
		auto succ_set = std::set<std::shared_ptr<Edge>>();
		successor_rel.emplace(n.first, succ_set);
		auto pred_set = std::set<std::shared_ptr<Edge>>();
		predecessor_rel.emplace(n.first, pred_set);

		if (n.second->is_entry) {
			current_state = n.second;
		}
	}
	if (current_state == nullptr) {
		std::cerr << "There seems to be no entry state to the witness automaton! Aborting validation." << std::endl;
		this->illegal_state = true;
		return;
	}
	for (auto const& trans: edges) {
		auto src = nodes.find(trans->source_id);
		if (src == nodes.end()) {
			std::cerr << "WARN: Did not find source node '" << trans->source_id << "', skipping." << std::endl;
			continue;
		}
		auto tar = nodes.find(trans->target_id);
		if (tar == nodes.end()) {
			std::cerr << "WARN: Did not find target node '" << trans->target_id << "', skipping." << std::endl;
			continue;
		}

		// fix startline, endline
		if (trans->end_line == 0) {
			trans->end_line = trans->start_line;
//            fprintf(stderr, "No endline definition for %s --> %s. Set to: %d\n", trans->source_id.c_str(), trans->target_id.c_str(), trans->start_line);
		}

		auto& node_successors = successor_rel.find(trans->source_id)->second;
		node_successors.insert(trans);
		auto& node_predecessors = predecessor_rel.find(trans->target_id)->second;
		node_predecessors.insert(trans);
	}
}

WitnessAutomaton::WitnessAutomaton() {
	std::shared_ptr<Node> n = std::make_shared<Node>();
	n->id = "node";
	n->is_entry = true;
	n->is_violation = false;
	nodes.insert(std::make_pair("node", n));
	auto e = std::make_shared<Edge>();
	e->start_line = 1;
	e->end_line = -1ul;
	e->source_id = "node";
	e->target_id = "node";

	current_state = n;

	auto succ_set = std::set<std::shared_ptr<Edge>>();
	succ_set.insert(e);
	successor_rel.emplace(n->id, succ_set);
	auto pred_set = std::set<std::shared_ptr<Edge>>();
	pred_set.insert(e);
	predecessor_rel.emplace(n->id, pred_set);

}

void WitnessAutomaton::printData() const {
	this->data.print();
}

void WitnessAutomaton::printRelations() const {
	std::cout << "Successor relation (" << successor_rel.size() << "):" << std::endl;
	for (auto const& n: successor_rel) {
		std::cout << n.first << "\t ----> \t";
		for (auto const& s: n.second) {
			std::cout << s->target_id << ", ";
		}
		std::cout << std::endl;
	}
	std::cout << "Predecessor relation (" << predecessor_rel.size() << "):" << std::endl;
	for (auto const& n: predecessor_rel) {
		std::cout << n.first << "\t <---- \t";
		for (auto const& s: n.second) {
			std::cout << s->source_id << ", ";
		}
		std::cout << std::endl;
	}
}

bool WitnessAutomaton::isInIllegalState() const {
	return this->illegal_state;
}

bool WitnessAutomaton::isInViolationState() const {
	return (current_state != nullptr) && current_state->is_violation;
}

bool WitnessAutomaton::isInSinkState() const {
	return (current_state != nullptr) && current_state->is_sink;
}

std::shared_ptr<Node> const& WitnessAutomaton::getCurrentState() const {
	return this->current_state;
}

bool WitnessAutomaton::wasVerifierErrorCalled() const {
	return this->verifier_error_called;
}

Data const& WitnessAutomaton::getData() const {
	return data;
}

void Node::print() const {
	std::cout << "id " << id << ": " << node_type;
	std::cout << ", th: " << thread_number;
	std::cout << ", f: " << is_frontier;
	std::cout << ", v: " << is_violation;
	std::cout << ", s: " << is_sink;
	std::cout << ", e: " << is_entry << std::endl;
}

void Edge::print() const {
	std::cout << source_id << " --> " << target_id << ": ";
	std::cout << "line: " << start_line;
	std::cout << ", file : " << origin_file << std::endl;
	std::cout << "\tsrc: " << source_code;
	std::cout << ", ret: " << return_from_function;
	std::cout << ", ent: " << enter_function;
	std::cout << ", ctrl: " << control << std::endl;
	std::cout << "\tassume: " << assumption;
	std::cout << ", scope: " << assumption_scope;
	std::cout << ", func: " << assumption_result_function;
	std::cout << ", loop: " << enterLoopHead << std::endl;
}

void Data::print() const {
	std::cout << "type: " << witness_type;
	std::cout << ", src: " << source_code_lang;
	std::cout << ", file: " << program_file;
	std::cout << ", arch: " << architecture << std::endl;
	std::cout << "hash: " << program_hash;
	std::cout << ", spec: " << specification;
	std::cout << ", prod: " << producer << std::endl;
}
