//
// Created by jan svejda on 3.4.19.
//

#ifndef NITWIT_WITNESS_HPP
#define NITWIT_WITNESS_HPP

#include <string>
#include "../utils/pugixml/pugixml.hpp"
#include <cstring>
#include <memory>

using namespace std;

shared_ptr<pugi::xml_document> parseGraphmlWitness(const string &filename);


#endif //NITWIT_WITNESS_HPP
