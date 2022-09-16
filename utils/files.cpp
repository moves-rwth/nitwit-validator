//
// Created by jan on 3.4.19.
//

#include "files.hpp"
#include <cerrno>
#include <cstring>

#include <sys/stat.h>

#include <iostream>
#include <fstream>
#include <sstream>

std::string readFile(const char *FileName, bool& error) {
    error = false;
    struct stat FileInfo;
    
    int const statResult = stat(FileName, &FileInfo);
    if (statResult != 0) {
        std::cerr << "Cannot read file '" << FileName << "', stat failed with error '" << strerror(errno) << "'." << std::endl;
        error = true;
        return "";
    }

    std::ifstream inStream(FileName);
    std::stringstream buffer;
    buffer << inStream.rdbuf();
    return buffer.str();
}
