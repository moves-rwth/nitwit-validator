#include <stdio.h>
#include <math.h>
void __VERIFIER_error(){printf("Error\n");}
double __VERIFIER_nondet_double(void){return 22.822146805041292;}
int __VERIFIER_nondet_int(void){return 0;}
float __VERIFIER_nondet_float(void){return 27;}
void __VERIFIER_assume(int expression){printf("Assume %d\n", expression);}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }


int blah() {
    printf("Blah should not print\n");
    return 7;
}

int main() {

    int i = 1 ? 0 : blah();
    int j = 0 ? blah() : 0;

    printf("i: %d, j: %d\n", i, j);
    return 0;
}