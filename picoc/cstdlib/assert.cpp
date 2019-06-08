//
// Created by jan on 5.5.19.
//

#include "../interpreter.hpp"


void Assert(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
    if (!Param[0]->Val->Integer)
        ProgramFailWithExitCode(Parser, 248, "assertion does not hold");
}

/* all Assert.h functions */
struct LibraryFunction AssertFunctions[] =
        {
                {Assert,  "void assert (int expression);"},
                {nullptr, nullptr}
        };
