#include <stdio.h>
#include <math.h>
 void __VERIFIER_error(){
    printf("Error\n");
}
/* Example from "Abstract Domains for Bit-Level Machine Integer and
   Floating-point Operations" by Miné, published in WING 12.
*/

int  __VERIFIER_nondet_int(void){
    return -3841;
}
void __VERIFIER_assume(int expression){
    printf("Assume %d\n", expression);
}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }

union u {
    int i[2];
    double d;
};

double cast(int i)
{
    union u x, y;
    x.i[0] = 0x43300000;
    y.i[0] = x.i[0];
    x.i[1] = 0x80000000;
    y.i[1] = i ^ x.i[1];

    double res = y.d - x.d;
    printf("Is nan: %d\n", isnan(res));
}

int main()
{
    int a;
    double r;

    a = __VERIFIER_nondet_int();
    __VERIFIER_assume(a >= -10000 && a <= 10000);

    r = cast(a);
    __VERIFIER_assert(r >= -10000. && r <= 10000.);
    return 0;
}
