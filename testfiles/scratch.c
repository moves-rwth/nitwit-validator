//extern void __VERIFIER_error();
#include <stdio.h>

int main()
{
    char const * c = "23";
    const int a = 90;
    const char * s = "23";
    double f = 2.0;
    const double * d = &f;

    int b = 12;
    b = 23;
    *s = 'j';
    c = "33224";
    c[3] = 0;
    return 0;
}

