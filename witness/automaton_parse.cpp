
//
// Created by jan svejda on 3.4.19.
//

#include "automaton.hpp"


shared_ptr<DefaultKeyValues> parseKeys(const pugi::xpath_node_set &keyNodeSet);
shared_ptr<DefaultKeyValues> getDefaultKeys();

map<string, shared_ptr<Node>>
parseNodes(const pugi::xpath_node_set &set, const shared_ptr<DefaultKeyValues> &defaultKeyValues);

void setNodeAttributes(const shared_ptr<Node> &node, const char *name, const char *value);

shared_ptr<Node> getDefaultNode(const shared_ptr<DefaultKeyValues> &def_values);

vector<shared_ptr<Edge>>
parseEdges(const pugi::xpath_node_set &set, const map<string,shared_ptr<Node>> graphNodes, const shared_ptr<DefaultKeyValues> &defaultKeyValues);

shared_ptr<Data> parseData(const pugi::xpath_node_set &set);

shared_ptr<WitnessAutomaton> WitnessAutomaton::automatonFromWitness(const shared_ptr<pugi::xml_document> &doc) {
    pugi::xml_node root = doc->root().first_child();
    if (!root || strcmp(root.name(), "graphml") != 0) {
        fprintf(stderr, " ### No graphml root element.\n");
        return make_shared<WitnessAutomaton>();
    }
    string xpath = "/graphml/key"; // TODO add attributes
    auto key_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));

    shared_ptr<DefaultKeyValues> default_key_values;
    if (key_result.empty()) {
        fprintf(stderr, " ### No graphml keys found!\n");
        default_key_values = getDefaultKeys();
    } else {
        default_key_values = parseKeys(key_result);
    }

    xpath = "/graphml/graph/node[@id]";
    auto node_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));
    if (node_result.empty()) {
        fprintf(stderr, " ### There should be at least 1 node!\n");
        return make_shared<WitnessAutomaton>();
    }

    xpath = "/graphml/graph/edge[@source and @target]";
    auto edge_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));
    if (edge_result.empty()) {
        fprintf(stderr, " ### There are no edges!\n");
        return make_shared<WitnessAutomaton>(); // TODO Could there be witnesses with no edges?
    }

    xpath = "/graphml/graph/data[@key]";
    auto graph_data_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));
    if (graph_data_result.empty()) {
        fprintf(stderr, " ### There are no graph data!\n");
//        return make_shared<WitnessAutomaton>();
    }

    auto nodes = parseNodes(node_result, default_key_values);
    // we need to ckeck for loopHead node due to different graph syntaxes
    auto edges = parseEdges(edge_result, nodes, default_key_values);
    auto data = parseData(graph_data_result);
    auto aut = make_shared<WitnessAutomaton>(nodes, edges, data);

    return aut;
}

shared_ptr<Data> parseData(const pugi::xpath_node_set &set) {
    auto d = make_shared<Data>();
    for (auto xpath_node: set) {
        if (xpath_node.node() == nullptr) continue;
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

shared_ptr<Edge> getDefaultEdge(const shared_ptr<DefaultKeyValues> &def_values) {
    auto e = make_shared<Edge>();

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
    // todo startline isn't an int though, but size_t. Is that ok?
    e->start_line = atoi(def_values->getDefault("startline").default_val.c_str());
    e->start_line = atoi(def_values->getDefault("endline").default_val.c_str());
    e->start_offset = atoi(def_values->getDefault("startoffset").default_val.c_str());
    e->end_offset = atoi(def_values->getDefault("endoffset").default_val.c_str());

    return e;
}

string replace_equals(const pugi::char_t *value) {
    string s(value);
    s.replace(s.begin(), s.end(), "==", "= ");
    return s;
}

void setEdgeAttributes(shared_ptr<Edge> &edge, const map<string,shared_ptr<Node>> nodes, const pugi::char_t *name, const pugi::char_t *value) {
    if (strcmp(name, "source") == 0) {
        edge->source_id = value;
    } else if (strcmp(name, "target") == 0) {
        edge->target_id = value;
        //check if we have an loopHead node
        if(nodes.find(value) != nodes.end() && nodes.find(value)->second->is_loopHead)
            edge->enterLoopHead = true;              
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
        edge->start_offset = atoi(value);
    } else if (strcmp(name, "endoffset") == 0) {
        edge->end_offset = atoi(value);
    } else if (strcmp(name, "startline") == 0) {
        edge->start_line = atoi(value);
    } else if (strcmp(name, "endline") == 0) {
        edge->end_line = atoi(value);
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
        fprintf(stderr, " ### Unrecognized edge attribute definition: %s\n", name);
#endif
    }
}


void parseEdgeProperties(const pugi::xml_node &node, const map<string,shared_ptr<Node>> nodes, shared_ptr<Edge> &edge) {
    for (auto child: node.children("data")) {
        auto key = child.attribute("key");
        if (!key.empty()) {
            setEdgeAttributes(edge, nodes, key.value(), child.text().get());
        }
    }
}

vector<shared_ptr<Edge>>
parseEdges(const pugi::xpath_node_set &set, const map<string,shared_ptr<Node>> graphNodes, const shared_ptr<DefaultKeyValues> &defaultKeyValues) {
    auto edges = vector<shared_ptr<Edge>>();
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

void parseNodeProperties(const pugi::xml_node &node, const shared_ptr<Node> &n) {
    for (auto child: node.children("data")) {
        auto key = child.attribute("key");
        if (!key.empty()) {
            setNodeAttributes(n, key.value(), child.text().get());
        }
    }
}

map<string, shared_ptr<Node>>
parseNodes(const pugi::xpath_node_set &set, const shared_ptr<DefaultKeyValues> &defaultKeyValues) {
    auto nodes = map<string, shared_ptr<Node>>();

    for (auto xpathNode: set) {
        // check wether xpathNode is a node element
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

shared_ptr<Node> getDefaultNode(const shared_ptr<DefaultKeyValues> &def_values) {
    auto n = make_shared<Node>();

    // strings
    n->invariant = def_values->getDefault("invariant").default_val;
    n->invariant_scope = def_values->getDefault("invariant.scope").default_val;
    n->node_type = def_values->getDefault("nodetype").default_val;

    // bools - default value for all is false, so only if default is "true", shall it be true
    n->is_frontier = (def_values->getDefault("frontier").default_val == "true");
    n->is_violation = (def_values->getDefault("violation").default_val == "true");
    n->is_entry = (def_values->getDefault("entry").default_val == "true");
    n->is_sink = (def_values->getDefault("sink").default_val == "true");
    n->is_loopHead = (def_values->getDefault("loopHead").default_val == "true");

    // integers
    string val = def_values->getDefault("thread").default_val;
    n->thread_number = atoi(val.c_str()); // todo thrd number isn't an int though, but size_t. Is that ok?

    return n;
}

void setNodeAttributes(const shared_ptr<Node> &node, const char *name, const char *value) {
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
        node->thread_number = atoi(value);
    } else {
#ifdef VERBOSE
        fprintf(stderr, " ### Unrecognized node attribute definition: %s\n", name);
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

shared_ptr<DefaultKeyValues> parseKeys(const pugi::xpath_node_set &keyNodeSet) {

    auto dkv = make_unique<DefaultKeyValues>();

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

shared_ptr<DefaultKeyValues> getDefaultKeys() {
    auto dkv = make_shared<DefaultKeyValues>();
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

void DefaultKeyValues::addKey(const Key &k) {
    this->default_keys.emplace(k.id, k);
}

Key DefaultKeyValues::getDefault(const string &id) const {
    auto it = this->default_keys.find(id);
    if (it == this->default_keys.end())
        return Key();
    else
        return it->second;
}

void DefaultKeyValues::print() const {
    for (auto item: this->default_keys) {
        Key::printKey(&item.second);
    }
}

WitnessAutomaton::WitnessAutomaton(const map<string, shared_ptr<Node>> &nodes, const vector<shared_ptr<Edge>> &edges,
                                   shared_ptr<Data> &data) :
        nodes((nodes)), edges((edges)), data(*data), current_state(nullptr), successor_rel(), predecessor_rel() {

    for (const auto &n: nodes) {
        auto succ_set = set<shared_ptr<Edge>>();
        successor_rel.emplace(n.first, succ_set);
        auto pred_set = set<shared_ptr<Edge>>();
        predecessor_rel.emplace(n.first, pred_set);

        if (n.second->is_entry) {
            current_state = n.second;
        }
    }
    if (current_state == nullptr) {
        fprintf(stderr, "There seems to be no entry state to the witness automaton! Aborting validation.");
        this->illegal_state = true;
        return;
    }
    for (const auto &trans: edges) {
        auto src = nodes.find(trans->source_id);
        if (src == nodes.end()) {
            fprintf(stderr, "WARN: Did not find source node '%s', skipping.", trans->source_id.c_str());
            continue;
        }
        auto tar = nodes.find(trans->target_id);
        if (tar == nodes.end()) {
            fprintf(stderr, "WARN: Did not find target node '%s', skipping.", trans->source_id.c_str());
            continue;
        }

        // fix startline, endline
        if (trans->end_line == 0) {
            trans->end_line = trans->start_line;
//            fprintf(stderr, "No endline definition for %s --> %s. Set to: %d\n", trans->source_id.c_str(), trans->target_id.c_str(), trans->start_line);
        }

        auto &node_successors = successor_rel.find(trans->source_id)->second;
        node_successors.insert(trans);
        auto &node_predecessors = predecessor_rel.find(trans->target_id)->second;
        node_predecessors.insert(trans);
    }
}

WitnessAutomaton::WitnessAutomaton() {
    shared_ptr<Node> n = make_shared<Node>();
    n->id = "node";
    n->is_entry = true;
    n->is_violation = false;
    nodes.insert(make_pair("node", n));
    auto e = make_shared<Edge>();
    e->start_line = 1;
    e->end_line = (unsigned long) -1;
    e->source_id = "node";
    e->target_id = "node";

    current_state = n;

    auto succ_set = set<shared_ptr<Edge>>();
    succ_set.insert(e);
    successor_rel.emplace(n->id, succ_set);
    auto pred_set = set<shared_ptr<Edge>>();
    pred_set.insert(e);
    predecessor_rel.emplace(n->id, pred_set);

}

void WitnessAutomaton::printData() const {
    this->data.print();
}

void WitnessAutomaton::printRelations() const {
    printf("Successor relation (%u):\n", successor_rel.size());
    for (const auto &n: successor_rel) {
        printf("%s\t ----> \t", n.first.c_str());
        for (const auto &s: n.second) {
            printf("%s, ", s->target_id.c_str());
        }
        printf("\n");
    }
    printf("Predecessor relation (%u):\n", predecessor_rel.size());
    for (const auto &n: predecessor_rel) {
        printf("%s\t <---- \t", n.first.c_str());
        for (const auto &s: n.second) {
            printf("%s, ", s->source_id.c_str());
        }
        printf("\n");
    }
}

bool WitnessAutomaton::isInIllegalState() const {
    return this->illegal_state;
}

bool WitnessAutomaton::isInViolationState() const {
    return current_state != nullptr && current_state->is_violation;
}

bool WitnessAutomaton::isInSinkState() const {
    return current_state != nullptr && current_state->is_sink;
}

const shared_ptr<Node> &WitnessAutomaton::getCurrentState() const {
    return this->current_state;
}

bool WitnessAutomaton::wasVerifierErrorCalled() const {
    return this->verifier_error_called;
}

const Data &WitnessAutomaton::getData() const {
    return data;
}

void Node::print() const {
    printf("id %s: %s, th: %zu, f: %d, v: %d, s: %d, e: %d\n", this->id.c_str(), this->node_type.c_str(),
           this->thread_number,
           this->is_frontier, this->is_violation, this->is_sink, this->is_entry);

}

void Edge::print() const {
    printf("%s --> %s: line: %zu, file: %s\n\tsrc: %s, ret: %s, ent: %s, ctrl: %s\n\tassume: %s, scope: %s, func: %s, loop: %d\n",
           this->source_id.c_str(), this->target_id.c_str(),
           this->start_line, this->origin_file.c_str(),
           this->source_code.c_str(),
           this->return_from_function.c_str(), this->enter_function.c_str(),
           this->control.c_str(),
           this->assumption.c_str(), this->assumption_scope.c_str(), this->assumption_result_function.c_str(),
           this->enterLoopHead
    );
}

void Data::print() const {
    printf("type: %s, src: %s, file: %s, arch: %s,\nhash: %s,\nspec: %s, prod: %s\n",
           this->witness_type.c_str(), this->source_code_lang.c_str(), this->program_file.c_str(),
           this->architecture.c_str(), this->program_hash.c_str(),
           this->specification.c_str(), this->producer.c_str()
    );
}
