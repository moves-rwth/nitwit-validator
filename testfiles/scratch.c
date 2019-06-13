#include <stdio.h>
#include <math.h>
void __VERIFIER_error(){printf("Error\n");}
double __VERIFIER_nondet_double(void){return 22.822146805041292;}
int __VERIFIER_nondet_int(void){return 0;}
float __VERIFIER_nondet_float(void){return 27;}
void __VERIFIER_assume(int expression){printf("Assume %d\n", expression);}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }


int main() {
    int i = 7;
    double d = 2.1;
    float f = __VERIFIER_nondet_float();

    {
        int i = 1;
        i = 230920;
        float f = d * i;

    }

    if (f == 27 && i == 7)
        __VERIFIER_error();

    printf("i: %d, f: %f", i, f);

    return 0;
}