//
// Created by jan svejda on 3.4.19.
//

#include "witness.hpp"

shared_ptr<pugi::xml_document> parseGraphmlWitness(const string &filename) {
    if (filename.empty()) {
        fprintf(stderr, "No witness file name provided.\n");
        return nullptr;
    }
    auto doc = make_shared<pugi::xml_document>();
    pugi::xml_node root;

    pugi::xml_parse_result parseResult = doc->load_file(filename.c_str());

    if (!parseResult) {
        fprintf(stderr, "Failed to parse witness file. Reason: %s\n", parseResult.description());
        return nullptr;
    }

    root = doc->root();

    if (!root || !root.first_child()) {
        fprintf(stderr, "Witness file is empty.\n");
        return nullptr;
    }

    if (strcmp(root.first_child().name(), "graphml") != 0) {
        fprintf(stderr, "Witness isn't a GraphML document. Root element=%s\n", root.name());
        return nullptr;
    }
    return doc;
}
