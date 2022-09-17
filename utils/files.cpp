//
// Created by jan on 3.4.19.
//

#include "files.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>

#include <sys/stat.h>

#include <iostream>
#include <fstream>
#include <regex>
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

    // Filter wacky stuff from the input...
    std::string result = buffer.str();
    std::regex re(R"((void\s+reach_error\(\)\s*\{\s*\(\s*\(\s*void\s*\)\s*sizeof\s*\(\s*\(\s*0\s*\)\s*\?\s*1\s*:\s*0\)\s*,\s*__extension__\s*\(\s*\{\s*if\s*\(\s*0\s*\)\s*;\s*else\s+__assert_fail\s*\(\s*"\d+"\s*,\s*"[^"]*"\s*,\s*\d+\s*,\s*__extension__\s+__PRETTY_FUNCTION__\s*\);\s*\}\)\s*\);\s*\}))");
    std::smatch match;
    if (std::regex_search(result, match, re) && match.size() > 1) {
        // Count the number of newlines in the match
        std::string const matchText = match.str(1);
        auto const numNewlines = std::count(matchText.begin(), matchText.end(), '\n');
        std::string replacementFunction = R"(void reach_error() { abort(); )" + std::string(numNewlines, '\n') + R"(})";
        result = std::regex_replace(result, re, replacementFunction);
        std::cerr << "Warning: Replaced wacky reach_error function definition containing ugly __extension__, offsets might be a little off!" << std::endl;
    }
    else {
        std::cout << "No changes done." << std::endl;
    }

    return result;
}
