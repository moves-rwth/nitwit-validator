#include <stdio.h>
#include <math.h>
void __VERIFIER_error(){printf("Error\n");}
double __VERIFIER_nondet_double(void){return 22.822146805041292;}
int __VERIFIER_nondet_int(void){return 0;}
float __VERIFIER_nondet_float(void){return 27;}
void __VERIFIER_assume(int expression){printf("Assume %d\n", expression);}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }

void (**frr)(int, int);
void (*fr)(int, int);

void blah (void asdf(int, int)) {
    asdf(1, 2);
    return;
}

void f(int a, int b){
    printf("blah\n");
    return;
}

int main() {
    blah(f);
    fr = &f;
//    frr = &fr;
    fr(1, 2);
//    (*frr)(1, 2);
    return 0;
}
