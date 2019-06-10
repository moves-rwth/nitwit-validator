/* string.h library for large systems - small embedded systems use clibrary.c instead */
#include "../interpreter.hpp"

#ifndef BUILTIN_MINI_STDLIB

static int trueValue = 1;
static int falseValue = 0;


/* structure definitions */
const char StdboolDefs[] = "typedef int bool;";

/* creates various system-dependent definitions */
void StdboolSetupFunc(Picoc *pc)
{
    /* defines */
    VariableDefinePlatformVar(pc, nullptr, "true", &pc->IntType, (union AnyValue *)&trueValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "false", &pc->IntType, (union AnyValue *)&falseValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "__bool_true_false_are_defined", &pc->IntType, (union AnyValue *)&trueValue, FALSE);
}

#endif /* !BUILTIN_MINI_STDLIB */
