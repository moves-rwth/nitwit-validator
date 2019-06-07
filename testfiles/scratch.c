#include <stdio.h>
#include <math.h>
 void __VERIFIER_error(){
    printf("Error\n");
}
/* Example from "Abstract Domains for Bit-Level Machine Integer and
   Floating-point Operations" by Miné, published in WING 12.
*/
double __VERIFIER_nondet_double(void){
    return 22.822146805041292;
}
int __VERIFIER_nondet_int(void){
    return 0;
}
void __VERIFIER_assume(int expression){
    printf("Assume %d\n", expression);
}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }

union double_int
{
    double d;
    int i;
};

double inv (double A)
{
    double xi, xsi, temp;
    signed int cond, exp;
    union double_int A_u, xi_u;
    A_u.d = A;
    exp = (signed int) ((A_u.i & 0x7FF00000) >> 20) - 1023;
    xi_u.d = 1;
    xi_u.i = ((1023-exp) << 20);
    printf("xi: %f\n", xi); xi = xi_u.d; printf("xi: %f\n", xi);
    cond = 1;
    while (cond) {
        printf("xsi: %f\n", xsi);xsi = 2*xi-A*xi*xi;printf("xsi: %f\n", xsi);
        temp = xsi-xi;
        cond = ((temp > 1e-10) || (temp < -1e-10));
        printf("xi: %f\n", xi); xi = xsi; printf("xi: %f\n", xi);
    }
    return xi;
}

int main()
{
    double a,r;

    a = __VERIFIER_nondet_double();
    __VERIFIER_assume(a >= 20. && a <= 30.);

    r = inv(a);

    __VERIFIER_assert(r >= 0 && r <= 0.06);
    return 0;
}
