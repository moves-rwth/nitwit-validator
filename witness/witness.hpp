//
// Created by jan svejda on 3.4.19.
//

#ifndef NITWIT_WITNESS_HPP
#define NITWIT_WITNESS_HPP

#include <string>
#include "../utils/pugixml/pugixml.hpp"
#include <cstring>
#include <memory>

std::shared_ptr<pugi::xml_document> parseGraphmlWitness(std::string const& filename);

enum class WitnessFormat {
    // Format 1.0, GraphML
    GraphML,
    // Format 2.x, YAML
    Yaml
};

// Detects the format from the file content, files that can not be read are reported as GraphML.
WitnessFormat detectWitnessFormat(std::string const& filename);


#endif //NITWIT_WITNESS_HPP
