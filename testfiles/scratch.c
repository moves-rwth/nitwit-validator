#include <stdio.h>
#include <math.h>
 void __VERIFIER_error(){
    printf("Error\n");
}
/* Example from "Abstract Domains for Bit-Level Machine Integer and
   Floating-point Operations" by Miné, published in WING 12.
*/

double __VERIFIER_nondet_double(void){
    return NAN;
}
void __VERIFIER_assume(int expression){
    printf("Assume %d\n", expression);
}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }


float cast(double d)
{
    double dmax;
    float f;
    printf("d nan: %d\n", isnan(d));

    if ( (((*(unsigned*)&d) & 0x7FF00000) >> 20) == 2047 ) return 0.f;

    ((unsigned*)&dmax)[0] = 0x47efffff;
    ((unsigned*)&dmax)[1] = 0xe0000000;

    if (d > dmax) {
        *(unsigned*)&f = 0x7f7fffff;
    }
    else if (-d > dmax) {
        *(unsigned*)&f = 0xff7fffff;
    }
    else {
        __VERIFIER_assert(d >= -3.41e38 && d <= 3.41e38);
        f = d;
    }

    return f;
}

int main()
{
    double a;
    float r;

    a = __VERIFIER_nondet_double();
    r = cast(a);
    return 0;
}
