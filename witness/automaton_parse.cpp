#include <utility>

#include <utility>

//
// Created by jan svejda on 3.4.19.
//

#include <cassert>
#include <cstring>
#include "automaton.hpp"


shared_ptr<DefaultKeyValues> parseKeys(const pugi::xpath_node_set &keyNodeSet);

map<string, shared_ptr<Node>>
parseNodes(const pugi::xpath_node_set &set, const shared_ptr<DefaultKeyValues> &defaultKeyValues);

void setNodeAttributes(const shared_ptr<Node> &node, const char *name, const char *value);

shared_ptr<Node> getDefaultNode(const shared_ptr<DefaultKeyValues> &def_values);

vector<shared_ptr<Edge>>
parseEdges(const pugi::xpath_node_set &set, const shared_ptr<DefaultKeyValues> &defaultKeyValues);

shared_ptr<Data> parseData(const pugi::xpath_node_set &set);

shared_ptr<Automaton> Automaton::automatonFromWitness(const shared_ptr<pugi::xml_document> &doc) {
    pugi::xml_node root = doc->root().first_child();
    if (!root || strcmp(root.name(), "graphml") != 0) {
        fprintf(stderr, "No graphml root element.");
        return nullptr;
    }
    string xpath = "/graphml/key"; // TODO add attributes
    auto key_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));

    if (key_result.empty()) {
        fprintf(stderr, "No graphml keys found!");
        return nullptr;
    }

    xpath = "/graphml/graph/node[@id]";
    auto node_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));
    if (node_result.empty()) {
        fprintf(stderr, "There should be at least 1 node!");
        return nullptr;
    }

    xpath = "/graphml/graph/edge[@source and @target]";
    auto edge_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));
    if (edge_result.empty()) {
        fprintf(stderr, "There are no edges!");
        return nullptr; // TODO Could there be witnesses with no edges?
    }

    xpath = "/graphml/graph/data[@key]";
    auto graph_data_result = doc->select_nodes(pugi::xpath_query(xpath.c_str()));
    if (graph_data_result.empty()) {
        fprintf(stderr, "There are no edges!");
        return nullptr; // TODO Could there be witnesses with no edges?
    }

    auto default_key_values = parseKeys(key_result);
    auto nodes = parseNodes(node_result, default_key_values);
    auto edges = parseEdges(edge_result, default_key_values);
    auto data = parseData(graph_data_result);
    auto aut = make_shared<Automaton>(nodes, edges, data);

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

void setEdgeAttributes(shared_ptr<Edge> &edge, const pugi::char_t *name, const pugi::char_t *value) {
    if (strcmp(name, "source") == 0) {
        edge->source_id = value;
    } else if (strcmp(name, "target") == 0) {
        edge->target_id = value;
    } else if (strcmp(name, "assumption") == 0) {
        edge->assumption = value;
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
    } else {
        fprintf(stderr, "I am missing an edge attribute definition: %s\n", name);
    }
}

void parseEdgeProperties(const pugi::xml_node &node, shared_ptr<Edge> &edge) {
    for (auto child: node.children("data")) {
        auto key = child.attribute("key");
        if (!key.empty()) {
            setEdgeAttributes(edge, key.value(), child.text().get());
        }
    }
}

vector<shared_ptr<Edge>>
parseEdges(const pugi::xpath_node_set &set, const shared_ptr<DefaultKeyValues> &defaultKeyValues) {
    auto edges = vector<shared_ptr<Edge>>();
    edges.reserve(set.size());

    for (auto xpathNode: set) {
        if (!xpathNode.node()) {
            continue;
        }

        auto e = getDefaultEdge(defaultKeyValues);
        pugi::xml_node node = xpathNode.node();
        for (auto attr: node.attributes()) {
            setEdgeAttributes(e, attr.name(), attr.value());
        }
        parseEdgeProperties(node, e);

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
    } else if (strcmp(name, "violation") == 0) {
        node->is_violation = (strcmp(value, "true") == 0);
    } else if (strcmp(name, "invariant") == 0) {
        node->invariant = value;
    } else if (strcmp(name, "invariant.scope") == 0) {
        node->invariant_scope = value;
    } else if (strcmp(name, "nodetype") == 0) {
        node->node_type = value;
    } else if (strcmp(name, "thread") == 0) {
        node->thread_number = atoi(value);
    } else {
        fprintf(stderr, "I am missing a node attribute definition: %s\n", name);
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

void DefaultKeyValues::addKey(const Key &k) {
    this->default_keys.emplace(k.id, k);
}

const Key DefaultKeyValues::getDefault(const string &id) const {
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

Automaton::Automaton(const map<string, shared_ptr<Node>> &nodes, const vector<shared_ptr<Edge>> &edges,
                     shared_ptr<Data> &data) :
        nodes((nodes)), edges((edges)), data(*data), current_state(nullptr), successor_rel(), predecessor_rel() {

    for (const auto &n: nodes) {
        auto succ_set = set<shared_ptr<Edge>>();
//        succ_set.insert(make_shared<Node>(n.second));
        successor_rel.emplace(n.first, succ_set);
        auto pred_set = set<shared_ptr<Edge>>();
//        pred_set.insert(make_shared<Node>(n.second));
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

        auto &node_successors = successor_rel.find(trans->source_id)->second;
        node_successors.insert(trans);
        auto &node_predecessors = predecessor_rel.find(trans->target_id)->second;
        node_predecessors.insert(trans);


    }
}

void Automaton::printData() const {
    this->data.print();
}

void Automaton::printRelations() const {
    printf("Successor relation (%lu):\n", successor_rel.size());
    for (const auto &n: successor_rel) {
        printf("%s\t ----> \t", n.first.c_str());
        for (const auto &s: n.second) {
            printf("%s, ", s->target_id.c_str());
        }
        printf("\n");
    }
    printf("Predecessor relation (%lu):\n", predecessor_rel.size());
    for (const auto &n: predecessor_rel) {
        printf("%s\t <---- \t", n.first.c_str());
        for (const auto &s: n.second) {
            printf("%s, ", s->source_id.c_str());
        }
        printf("\n");
    }
}

bool Automaton::isInIllegalState() const {
    return this->illegal_state;
}

bool Automaton::isInViolationState() const {
    return current_state != nullptr && current_state->is_violation;
}

bool Automaton::isInSinkState() const {
    return current_state != nullptr && current_state->is_sink;
}

const shared_ptr<Node> &Automaton::getCurrentState() const {
    return this->current_state;
}

bool Automaton::wasVerifierErrorCalled() const {
    return this->verifier_error_called;
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


ProgramState::ProgramState(string originFile, string enterFunction, string returnFromFunction,
                           string sourceCode, ConditionControl control, size_t startLine, bool enterLoopHead)
        : origin_file(std::move(originFile)), enter_function(std::move(enterFunction)),
          return_from_function(std::move(returnFromFunction)),
          source_code(std::move(sourceCode)), control(std::move(control)), start_line(startLine),
          enterLoopHead(enterLoopHead) {}

ProgramState::ProgramState() = default;
