#include <stdio.h>
//#include <math.h>
void __VERIFIER_error(){printf("Error\n");}
double __VERIFIER_nondet_double(void){return 22.822146805041292;}
int __VERIFIER_nondet_int(void){return 0;}
float __VERIFIER_nondet_float(void){return 27;}
void __VERIFIER_assume(int expression){printf("Assume %d\n", expression);}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }

struct bf {
    int a: 1;
    int b: 2;
    int c: 3;
    int d : 4, e: 4;
};

int main ()
{
    struct bf s = {9, 9, 9, 9, 9};
    struct bf s2;
    if (s.a == -1 && s.b == 1 && s.c == 1 && s.d == -7 && s.e == -7){
        __VERIFIER_error();
    }
    printf("%d, %d, %d, %d, %d\n", s.a, s.b, s.c, s.d, s.e);
    s.a += 1;
    s.b += 1;
    s.c += 1;
    s.d += 1;
    s.e += 1;
    printf("%d, %d, %d, %d, %d\n", s.a, s.b, s.c, s.d, s.e);
    s.a += -2;
    s.b += -2;
    s.c += -2;
    s.d += -2;
    s.e += -2;
    printf("%d, %d, %d, %d, %d\n", s.a, s.b, s.c, s.d, s.e);
    s2 = s;
    printf("%d, %d, %d, %d, %d\n", s2.a, s2.b, s2.c, s2.d, s2.e);
    printf("%d\n", signbit(-1.3));
    printf("%d\n", isnan(nan("")));

}
