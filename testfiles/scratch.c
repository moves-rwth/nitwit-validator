#include <stdio.h>
#include <math.h>
void __VERIFIER_error(){printf("Error\n");}
double __VERIFIER_nondet_double(void){return 22.822146805041292;}
int __VERIFIER_nondet_int(void){return 0;}
float __VERIFIER_nondet_float(void){return 27;}
void __VERIFIER_assume(int expression){printf("Assume %d\n", expression);}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }

struct dummy {
    int a, b;
};


int main ()
{
    struct dummy d1, d2;
    struct dummy *pd =  0 ? &d1 : &d2;
    if (pd == &d2) {
        pd->a = 0;
    } else {
        pd->b = 0;
    }



    return 0;
}
