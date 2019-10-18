//
// Created by jan svejda on 3.4.19.
//

#ifndef NITWIT_AUTOMATON_HPP
#define NITWIT_AUTOMATON_HPP

#include "../picoc/picoc.hpp"

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

using namespace std;
#ifdef VERBOSE
#define cw_verbose printf
#else

void cw_verbose(const string& Format, ...);
#endif


class Node;

class Edge;

class WitnessAutomaton;

class DefaultKeyValues;

struct Data;

class ProgramState;

class Node {
public:
    string id;
    string node_type;
    string invariant;
    string invariant_scope;
    bool is_entry{};
    bool is_violation{};
    bool is_sink{};
    bool is_frontier{};
    size_t thread_number{};

    void print() const;
};


class Edge {
public:
    string source_id;
    string target_id;
    string origin_file;
    string assumption;
    string assumption_scope;
    string assumption_result_function;
    string enter_function;
    string return_from_function;
    string source_code;
    string control;
    ConditionControl controlCondition;
    size_t start_line;
    size_t end_line;
    size_t start_offset;
    size_t end_offset;
    bool enterLoopHead;

    void print() const;
};

struct Key {
    string name;
    string type;
    string for_;
    string id;
    string default_val;

    Key() : name(""), type(""), for_(""), id(""), default_val("") {}

    Key(string name, string type, string for_, string id, string defaultVal) :
            name(std::move(name)), type(std::move(type)), for_(std::move(for_)),
            id(std::move(id)), default_val(std::move(defaultVal)) {}

    static void printKey(Key *k) {
        printf("name: %s\ttype: %s\tfor: %s\tid: %s\tdef: %s\n", k->name.c_str(), k->type.c_str(), k->for_.c_str(),
               k->id.c_str(), k->default_val.c_str());
    }
};

class DefaultKeyValues {
    map<string, Key> default_keys;

public:
    void addKey(const Key &k);

    Key getDefault(const string &id) const;

    void print() const;
};

struct Data {
    string source_code_lang;
    string program_file;
    string program_hash;
    string specification;
    string architecture;
    string producer;
    string witness_type;

    void print() const;
};

class WitnessAutomaton {

    map<string, shared_ptr<Node>> nodes;
    vector<shared_ptr<Edge>> edges;
    Data data;
    shared_ptr<Node> current_state;

    map<string, set<shared_ptr<Edge>>> successor_rel;
    map<string, set<shared_ptr<Edge>>> predecessor_rel;
    bool illegal_state = false;
    bool verifier_error_called = false;
public:
    WitnessAutomaton(const map<string, shared_ptr<Node>> &nodes, const vector<shared_ptr<Edge>> &edges,
                     shared_ptr<Data> &data);

    WitnessAutomaton();

    void printData() const;

    void printRelations() const;

    void consumeState(ParseState *state);

    bool isInIllegalState() const;

    static shared_ptr<WitnessAutomaton> automatonFromWitness(const shared_ptr<pugi::xml_document> &doc);

    bool isInViolationState() const;

    bool isInSinkState() const;

    bool wasVerifierErrorCalled() const;

    const shared_ptr<Node> &getCurrentState() const;

    bool canTransitionFurther();
};


#endif //NITWIT_AUTOMATON_HPP
