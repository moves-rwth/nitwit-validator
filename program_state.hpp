//
// Created by jan on 11.4.19.
//

#ifndef CWVALIDATOR_STATE_HPP
#define CWVALIDATOR_STATE_HPP


#include <cstddef>
#include <string>
#include <cstring>
#include <memory>

using namespace std;

class ProgramState {
public:
    string origin_file;
    string enter_function;
    string return_from_function;
    string source_code;
    string control;
    size_t start_line{};
    bool enterLoopHead{};

    ProgramState(string originFile, string enterFunction, string returnFromFunction,
                 string sourceCode, string control, size_t startLine, bool enterLoopHead);

    ProgramState();
};

void handleDebug(const struct ParseState *ps);

#endif //CWVALIDATOR_STATE_HPP
