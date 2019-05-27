extern void __VERIFIER_error();
#include <stdio.h>


int func1(char x){
    printf("1: %c\n", x);
    return x;
}

double error(char x, int y, short z) {
    __VERIFIER_error();
    return 1.0;
}

int main()
{
    int (*x)(char) = &func1;
    (*x)('2');
    x = NULL;
    return 0;
}

