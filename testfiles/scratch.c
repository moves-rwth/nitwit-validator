#include <stdio.h>
#include <math.h>
void __VERIFIER_error(){printf("Error\n");}
double __VERIFIER_nondet_double(void){return 22.822146805041292;}
int __VERIFIER_nondet_int(void){return 0;}
void __VERIFIER_assume(int expression){printf("Assume %d\n", expression);}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }


int main() {
    unsigned int allOne = -1;

    int castToInt = allOne; printf("c2i: %d\n", castToInt);
    long castToLong = allOne; printf("c2l: %ld\n", castToLong);
    long castToLong2 = castToInt; printf("c2l_2: %ld\n", castToLong2);
    unsigned long castToULong = allOne; printf("c2ul: %lu\n", castToULong);
    printf("Size long: %d, ulong: %d, int: %d\n", sizeof(long), sizeof(unsigned long), sizeof(int));

    if (castToInt == -1 && castToLong == 4294967295UL &&
        castToLong2 == -1 && castToULong == 4294967295UL) {
        goto ERROR;
    }

    return (0);
    ERROR: __VERIFIER_error();
    return (-1);
}

