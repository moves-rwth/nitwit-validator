//
// Created by jan on 11.4.19.
//

#include "witness/automaton.hpp"
#include "witness/witness.hpp"
#include "utils/files.hpp"
#include "picoc/picoc.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <utility>
#include "program_state.hpp"

ProgramState::ProgramState(string originFile, string enterFunction, string returnFromFunction,
                           string sourceCode, string control, size_t startLine, bool enterLoopHead)
        : origin_file(std::move(originFile)), enter_function(std::move(enterFunction)),
          return_from_function(std::move(returnFromFunction)),
          source_code(std::move(sourceCode)), control(std::move(control)), start_line(startLine),
          enterLoopHead(enterLoopHead) {}

ProgramState::ProgramState() = default;
