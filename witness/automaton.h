//
// Created by jan svejda on 3.4.19.
//

#ifndef CWVALIDATOR_AUTOMATON_H
#define CWVALIDATOR_AUTOMATON_H

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <stdbool.h>


struct Node {
    const char * id;
    const char * node_type;
    bool is_entry;
    bool is_violation;
    bool is_sink;
    bool is_frontier;
    size_t thread_number;
};

typedef struct Edge {
    const char * source_id;
    const char * target_id;
    const char * origin_file;
    size_t start_line;
    const char * assumption;
    const char * assumption_scope;
    bool entersLoopHead;
} Edge;

typedef struct Automaton {
    const char * filename;
    struct Node * nodes;
    struct Edge * edges;
} Automaton;

typedef struct defkeyvalues {
    int s;
} DefaultKeyValues;

struct Automaton * automatonFromWitness(xmlDocPtr doc);

#endif //CWVALIDATOR_AUTOMATON_H
