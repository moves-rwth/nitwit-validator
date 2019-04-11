//
// Created by jan svejda on 3.4.19.
//

#ifndef CWVALIDATOR_WITNESS_HPP
#define CWVALIDATOR_WITNESS_HPP

#include <string>
#include "../utils/pugixml/pugixml.hpp"
#include <cstring>
#include <memory>

using namespace std;

shared_ptr<pugi::xml_document> parseGraphmlWitness(const string &filename);


#endif //CWVALIDATOR_WITNESS_HPP
