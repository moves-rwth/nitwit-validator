//
// Created by jan svejda on 3.4.19.
//

#ifndef CWVALIDATOR_AUTOMATON_HPP
#define CWVALIDATOR_AUTOMATON_HPP

#include <stdbool.h>
#include <stddef.h>
#include "../utils/pugixml/pugixml.hpp"
#include <string>

using namespace std;

class Node {
public:
    const char *id;
    const char *node_type;
    bool is_entry;
    bool is_violation;
    bool is_sink;
    bool is_frontier;
    size_t thread_number;
};

class Edge {
public:
    const char *source_id;
    const char *target_id;
    const char *origin_file;
    size_t start_line;
    const char *assumption;
    const char *assumption_scope;
    bool entersLoopHead;
};

class Automaton {
public:
    const char *filename;
    Node *nodes;
    Edge *edges;
};

typedef struct defkeyvalues {
    int s;
} DefaultKeyValues;

Automaton *automatonFromWitness(const pugi::xml_document &doc);

#endif //CWVALIDATOR_AUTOMATON_HPP
