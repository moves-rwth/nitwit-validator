//
// Created by jan svejda on 3.4.19.
//

#include "witness.hpp"
#include <fstream>
#include <iostream>

WitnessFormat detectWitnessFormat(std::string const& filename) {
    std::ifstream file(filename, std::ios::binary);
    char c;
    while (file.get(c)) {
        unsigned char const u = static_cast<unsigned char>(c);
        // skip whitespace and a UTF-8 byte order mark
        if (u == ' ' || u == '\t' || u == '\r' || u == '\n' || u == 0xEF || u == 0xBB || u == 0xBF) {
            continue;
        }
        // GraphML is XML, which starts with a declaration, comment or element
        return (c == '<') ? WitnessFormat::GraphML : WitnessFormat::Yaml;
    }
    return WitnessFormat::GraphML;
}

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
