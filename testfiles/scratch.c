#include <stdio.h>

int func1(char x){
    printf("1: %c\n", x);
    return x;
}

int func2(char y) {
    printf("2: %c\n", y);
    return y;
}

int main()
{
    int n = 1;
    int *p = &n;
    int (*y)(char) = &func2;
    int (*x)(char);
    y('2');
    x = &func1;
    x('1');
    x = &func2;
    x('2');
//    (*x)('f');
//    x = NULL;
//    x = func1;
//    x('F');
    return 0;
}

