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

void VerifierNonDet(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
    ReturnValue->IsNonDet = TRUE;
}
/* handy structure definitions */
const char VerifDefs[] = "";

/* all verif.h functions */
struct LibraryFunction VerifFunctions[] =
        {
                {VerifierError,  "void __VERIFIER_error();"},
                {VerifierAssume, "void __VERIFIER_assume(int a);"},

                {VerifierNonDet, "int __VERIFIER_nondet_int();"},
                {VerifierNonDet, "unsigned int __VERIFIER_nondet_uint();"},
                {VerifierNonDet, "unsigned short __VERIFIER_nondet_ushort();"},
                {VerifierNonDet, "short __VERIFIER_nondet_short();"},
                {VerifierNonDet, "long __VERIFIER_nondet_long();"},
                {VerifierNonDet, "unsigned long __VERIFIER_nondet_ulong();"},
                {VerifierNonDet, "char __VERIFIER_nondet_char();"},
                {VerifierNonDet, "unsigned char __VERIFIER_nondet_uchar();"},
                {VerifierNonDet, "double __VERIFIER_nondet_double();"},
//                {VerifierNonDet, "bool __VERIFIER_nondet_bool();"},
                {NULL, NULL}
        };

/* creates various system-dependent definitions */
void VerifSetupFunc(Picoc *pc) {
}

/*
__VERIFIER_error(): For checking (un)reachability we use the function __VERIFIER_error() (see property 'Error Function
 Unreachability' above). The verification tool can assume the following implementation:
void __VERIFIER_error() { abort(); }
Hence, a function call __VERIFIER_error() never returns and in the function __VERIFIER_error() the program terminates.

__VERIFIER_assume(expression): A verification tool can assume that a function call __VERIFIER_assume(expression) has
the following meaning: If 'expression' is evaluated to '0', then the function loops forever, otherwise the function
returns (no side effects). The verification tool can assume the following implementation:
void __VERIFIER_assume(int expression) { if (!expression) { LOOP: goto LOOP; }; return; }

__VERIFIER_nondet_X(): In order to model nondeterministic values, the following functions can be assumed to return an
arbitrary value of the indicated type: __VERIFIER_nondet_X() (and nondet_X(), deprecated) with X in {bool, char, int,
float, double, loff_t, long, pchar, pointer, pthread_t, sector_t, short, size_t, u32, uchar, uint, ulong, unsigned,
ushort} (no side effects, pointer for void *, etc.). The verification tool can assume that the functions are implemented
according to the following template:
X __VERIFIER_nondet_X() { X val; return val; }
 */