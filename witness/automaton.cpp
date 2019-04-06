//
// Created by jan svejda on 3.4.19.
//

#include <assert.h>
#include <cstring>
#include "automaton.hpp"


struct Key {
    string name;
    string type;
    string for_;
    string id;
    string default_val;

    Key() : name(""), type(""), for_(""), id(""), default_val("") {}

    static void printKey(Key *k) {
        printf("name: %s\ttype: %s\tfor: %s\tid: %s\tdef: %s\n", k->name.c_str(), k->type.c_str(), k->for_.c_str(),
               k->id.c_str(), k->default_val.c_str());
    }
};


DefaultKeyValues *parseKeys(const pugi::xpath_node_set &keyNodeSet) ;


Automaton *automatonFromWitness(const pugi::xml_document &doc) {
    pugi::xml_node root = doc.root().first_child();
    if (!root || strcmp(root.name(), "graphml") != 0) {
        fprintf(stderr, "No graphml root element.");
        return nullptr;
    }
    string xpath = "/graphml/key";
    auto key_result = doc.select_nodes(pugi::xpath_query(xpath.c_str()));


    if (key_result.empty()) {
        fprintf(stderr, "No graphml keys found!");
        return nullptr;
    }
    DefaultKeyValues *default_key_values = parseKeys(key_result);

    delete default_key_values;
    return nullptr; // TODO return true automaton
}

void setAttributes(Key *key, const char * value) {
    if (strcmp(value, "attr.name") == 0) {
        key->name = value;
    } else if (strcmp(value, "attr.type") == 0) {
        key->type = value;
    } else if (strcmp(value, "for") == 0) {
        key->for_ = value;
    } else if (strcmp(value, "id") == 0) {
        key->id = value;
    }
}

DefaultKeyValues *parseKeys(const pugi::xpath_node_set &keyNodeSet) {

    for (pugi::xpath_node xpath_node: keyNodeSet) {
        if (!xpath_node.node()){
            continue;
        }
        pugi::xml_node node = xpath_node.node();
        Key *k = new Key();

        for (auto attrIt: node.attributes()) {
            setAttributes(k, attrIt.value());
        }

        pugi::xml_node cur = node.child("default");
        if (cur) {
            // has default value
            k->default_val = cur.value();
        }

        delete k;

    }

    return nullptr;
}
