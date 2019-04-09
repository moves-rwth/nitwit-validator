#include <utility>

#include <utility>

//
// Created by jan svejda on 3.4.19.
//

#include <cassert>
#include <cstring>
#include "automaton_parse.hpp"


shared_ptr<DefaultKeyValues> parseKeys(const pugi::xpath_node_set &keyNodeSet);

vector<Node> parseNodes(const pugi::xpath_node_set &set, const shared_ptr<DefaultKeyValues>& defaultKeyValues);

void setNodeAttributes(Node &node, const char *name, const char *value);

Node getDefaultNode(const shared_ptr<DefaultKeyValues>& def_values);

vector<Edge> parseEdges(const pugi::xpath_node_set &set, const shared_ptr<DefaultKeyValues>& defaultKeyValues);

shared_ptr<Data> parseData(const pugi::xpath_node_set &set);

shared_ptr<Automaton> Automaton::automatonFromWitness(const pugi::xml_document &doc) {
    pugi::xml_node root = doc.root().first_child();
    if (!root || strcmp(root.name(), "graphml") != 0) {
        fprintf(stderr, "No graphml root element.");
        return nullptr;
    }
    string xpath = "/graphml/key"; // TODO add attributes
    auto key_result = doc.select_nodes(pugi::xpath_query(xpath.c_str()));

    if (key_result.empty()) {
        fprintf(stderr, "No graphml keys found!");
        return nullptr;
    }

    xpath = "/graphml/graph/node[@id]";
    auto node_result = doc.select_nodes(pugi::xpath_query(xpath.c_str()));
    if (node_result.empty()) {
        fprintf(stderr, "There should be at least 1 node!");
        return nullptr;
    }

    xpath = "/graphml/graph/edge[@source and @target]";
    auto edge_result = doc.select_nodes(pugi::xpath_query(xpath.c_str()));
    if (edge_result.empty()) {
        fprintf(stderr, "There are no edges!");
        return nullptr; // TODO Could there be witnesses with no edges?
    }

    xpath = "/graphml/graph/data[@key]";
    auto graph_data_result = doc.select_nodes(pugi::xpath_query(xpath.c_str()));
    if (graph_data_result.empty()) {
        fprintf(stderr, "There are no edges!");
        return nullptr; // TODO Could there be witnesses with no edges?
    }

    shared_ptr<DefaultKeyValues> default_key_values = parseKeys(key_result);
    vector<Node> nodes = parseNodes(node_result, default_key_values);
    vector<Edge> edges = parseEdges(edge_result, default_key_values);
    auto data = parseData(graph_data_result);
    auto aut = make_shared<Automaton>(nodes, edges, data);

    return aut;
}

shared_ptr<Data> parseData(const pugi::xpath_node_set &set) {
    auto d = make_shared<Data>();
    for (auto xpath_node: set) {
        if (xpath_node.node() == nullptr) continue;
        auto attr = xpath_node.node().attribute("key");
        if (attr.empty()){
            continue;
        }
        const char * name = attr.value();
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

Edge getDefaultEdge(const shared_ptr<DefaultKeyValues>& def_values) {
    auto e = Edge();

    // strings
    e.assumption = def_values->getDefault("assumption").default_val;
    e.assumption_scope = def_values->getDefault("assumption.scope").default_val;
    e.assumption_result_function = def_values->getDefault("assumption.resultfunction").default_val;
    e.origin_file = def_values->getDefault("originfile").default_val;
//    e.source_id = def_values->getDefault("nodetype").default_val;
//    e.target_id = def_values->getDefault("nodetype").default_val;
    e.control = def_values->getDefault("control").default_val;
    e.enter_function = def_values->getDefault("enterFunction").default_val;
    e.return_from_function = def_values->getDefault("returnFrom").default_val;
    e.source_code = def_values->getDefault("sourcecode").default_val;

    // bools - default value for all is false, so only if default is "true", shall it be true
    e.enterLoopHead = (def_values->getDefault("enterLoopHead").default_val == "true");

    // integers
    e.start_line = atoi(def_values->getDefault(
            "startline").default_val.c_str()); // todo startline isn't an int though, but size_t. Is that ok?

    return e;
}

void setEdgeAttributes(Edge &edge, const pugi::char_t *name, const pugi::char_t *value) {
    if (strcmp(name, "source") == 0) {
        edge.source_id = value;
    } else if (strcmp(name, "target") == 0) {
        edge.target_id = value;
    } else if (strcmp(name, "assumption") == 0) {
        edge.assumption = value;
    } else if (strcmp(name, "assumption.scope") == 0) {
        edge.assumption_scope = value;
    } else if (strcmp(name, "assumption.resultfunction") == 0) {
        edge.assumption_result_function = value;
    } else if (strcmp(name, "originfile") == 0) {
        edge.origin_file = value;
    } else if (strcmp(name, "control") == 0) {
        edge.control = value;
    } else if (strcmp(name, "enterFunction") == 0) {
        edge.enter_function = value;
    } else if (strcmp(name, "returnFrom") == 0) {
        edge.return_from_function = value;
    } else if (strcmp(name, "sourcecode") == 0) {
        edge.source_code = value;
    } else if (strcmp(name, "enterLoopHead") == 0) {
        edge.enterLoopHead = strcmp(value, "true") == 0;
    } else if (strcmp(name, "startline") == 0) {
        edge.start_line = atoi(value);
    } else {
        fprintf(stderr, "I am missing an edge attribute definition: %s\n", name);
    }
}

void parseEdgeProperties(const pugi::xml_node &node, Edge &edge) {
    for (auto child: node.children("data")) {
        auto key = child.attribute("key");
        if (!key.empty()) {
            setEdgeAttributes(edge, key.value(), child.text().get());
        }
    }
}

vector<Edge> parseEdges(const pugi::xpath_node_set &set, const shared_ptr<DefaultKeyValues>& defaultKeyValues) {
    vector<Edge> edges = vector<Edge>();
    edges.reserve(set.size());

    for (auto xpathNode: set) {
        if (!xpathNode.node()) {
            continue;
        }

        Edge e = getDefaultEdge(defaultKeyValues);
        pugi::xml_node node = xpathNode.node();
        for (auto attr: node.attributes()) {
            setEdgeAttributes(e, attr.name(), attr.value());
        }
        parseEdgeProperties(node, e);

        edges.push_back(e);
    }

    return edges;
}

void parseNodeProperties(const pugi::xml_node &node, Node &n) {
    for (auto child: node.children("data")) {
        auto key = child.attribute("key");
        if (!key.empty()) {
            setNodeAttributes(n, key.value(), child.text().get());
        }
    }
}

vector<Node> parseNodes(const pugi::xpath_node_set &set, const shared_ptr<DefaultKeyValues>& defaultKeyValues) {
    vector<Node> nodes = vector<Node>();
    nodes.reserve(set.size());

    for (auto xpathNode: set) {
        if (!xpathNode.node()) {
            continue;
        }

        Node n = getDefaultNode(defaultKeyValues);
        pugi::xml_node node = xpathNode.node();
        for (auto attr: node.attributes()) {
            setNodeAttributes(n, attr.name(), attr.value());
        }
        parseNodeProperties(node, n);

        nodes.push_back(n);
    }

    return nodes;
}

Node getDefaultNode(const shared_ptr<DefaultKeyValues>& def_values) {
    auto n = Node();

    // strings
    n.invariant = def_values->getDefault("invariant").default_val;
    n.invariant_scope = def_values->getDefault("invariant.scope").default_val;
    n.node_type = def_values->getDefault("nodetype").default_val;

    // bools - default value for all is false, so only if default is "true", shall it be true
    n.is_frontier = (def_values->getDefault("frontier").default_val == "true");
    n.is_violation = (def_values->getDefault("violation").default_val == "true");
    n.is_entry = (def_values->getDefault("entry").default_val == "true");
    n.is_sink = (def_values->getDefault("sink").default_val == "true");

    // integers
    string val = def_values->getDefault("thread").default_val;
    n.thread_number = atoi(val.c_str()); // todo thrd number isn't an int though, but size_t. Is that ok?

    return n;
}

void setNodeAttributes(Node &node, const char *name, const char *value) {
    if (strcmp(name, "id") == 0) {
        node.id = value;
    } else if (strcmp(name, "entry") == 0) {
        node.is_entry = (strcmp(value, "true") == 0);
    } else if (strcmp(name, "sink") == 0) {
        node.is_sink = (strcmp(value, "true") == 0);
    } else if (strcmp(name, "frontier") == 0) {
        node.is_frontier = (strcmp(value, "true") == 0);
    } else if (strcmp(name, "violation") == 0) {
        node.is_violation = (strcmp(value, "true") == 0);
    } else if (strcmp(name, "invariant") == 0) {
        node.invariant = value;
    } else if (strcmp(name, "invariant.scope") == 0) {
        node.invariant_scope = value;
    } else if (strcmp(name, "nodetype") == 0) {
        node.node_type = value;
    } else if (strcmp(name, "thread") == 0) {
        node.thread_number = atoi(value);
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

void DefaultKeyValues::print() {
    for (auto item: this->default_keys) {
        Key::printKey(&item.second);
    }
}

Automaton::Automaton(vector<Node> nodes, vector<Edge> edges, shared_ptr<Data>& data):
        nodes(std::move(nodes)), edges(std::move(edges)), data(*data)
{

}

void Node::print() {
    printf("id %s: %s, th: %zu, f: %d, v: %d, s: %d, e: %d\n", this->id.c_str(), this->node_type.c_str(),
           this->thread_number,
           this->is_frontier, this->is_violation, this->is_sink, this->is_entry);

}

void Edge::print() {
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

void Data::print() {
    printf("type: %s, src: %s, file: %s, arch: %s,\nhash: %s,\nspec: %s, prod: %s\n",
        this->witness_type.c_str(), this->source_code_lang.c_str(), this->program_file.c_str(),
        this->architecture.c_str(), this->program_hash.c_str(),
        this->specification.c_str(), this->producer.c_str()
    );
}
