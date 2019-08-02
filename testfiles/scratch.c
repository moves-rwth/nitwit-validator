#include <stdio.h>
#include <math.h>
void __VERIFIER_error(){printf("Error\n");}
double __VERIFIER_nondet_double(void){return 22.822146805041292;}
int __VERIFIER_nondet_int(void){return 0;}
float __VERIFIER_nondet_float(void){return 27;}
void __VERIFIER_assume(int expression){printf("Assume %d\n", expression);}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }

void init (int x[3]) {
    x[0] = 1;
    x[1] = 2;
    x[2] = 3;
};


int main ()
{
    int x[3];
    init(x);
    printf("%d, %d, %d\n", x[0], x[1], x[2]);
}
