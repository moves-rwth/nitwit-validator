//
// Created by jan svejda on 3.4.19.
//

#include "witness.hpp"
#include <iostream>

std::shared_ptr<pugi::xml_document> parseGraphmlWitness(std::string const& filename) {
    if (filename.empty()) {
        std::cerr << "No witness file name provided." << std::endl;
        return nullptr;
    }
    auto doc = std::make_shared<pugi::xml_document>();
    pugi::xml_node root;

    pugi::xml_parse_result parseResult = doc->load_file(filename.c_str());

    if (!parseResult) {
        std::cerr << "Failed to parse witness file. Reason: " << parseResult.description() << std::endl;
        return nullptr;
    }

    root = doc->root();

    if (!root || !root.first_child()) {
        std::cerr << "Witness file is empty." << std::endl;
        return nullptr;
    }

    if (strcmp(root.first_child().name(), "graphml") != 0) {
        std::cerr << "Witness isn't a GraphML document. Root element=" << root.name() << std::endl;
        return nullptr;
    }
    return doc;
}
