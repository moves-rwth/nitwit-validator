//
// Created by jan on 5.5.19.
//

#include "../interpreter.h"

void VerifierError(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
    Parser->VerifierErrorCalled = TRUE;
}

void VerifierAssume(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
    if (!Param[0]->Val->Integer)
        ProgramFailWithExitCode(Parser, 247, "assumption does not hold");
}

/* handy structure definitions */
const char VerifDefs[] = "";

/* all verif.h functions */
struct LibraryFunction VerifFunctions[] =
        {
                {VerifierError,  "void __VERIFIER_error();"},
                {VerifierAssume, "void __VERIFIER_assume(int a);"},
                {NULL, NULL}
        };

/* creates various system-dependent definitions */
void VerifSetupFunc(Picoc *pc) {
}
