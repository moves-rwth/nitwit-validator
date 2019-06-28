#include <stdio.h>
#include <math.h>
void __VERIFIER_error(){printf("Error\n");}
double __VERIFIER_nondet_double(void){return 22.822146805041292;}
int __VERIFIER_nondet_int(void){return 0;}
float __VERIFIER_nondet_float(void){return 27;}
void __VERIFIER_assume(int expression){printf("Assume %d\n", expression);}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }

typedef int Char;


Char *tmp;

int glob2 (Char *pathbuf, Char *pathlim)
{
    Char *p;

    for (p = pathbuf; p <= pathlim; p++) {

        __VERIFIER_assert(p<=tmp);
        *p = 1;
    }

    return 0;
}

int main ()
{
    Char pathbuf[1 +1];

    Char *bound = pathbuf + sizeof(pathbuf) - 1;

    tmp = pathbuf + sizeof(pathbuf)/sizeof(*pathbuf) - 1;

    glob2 (pathbuf, bound);

    return 0;
}
