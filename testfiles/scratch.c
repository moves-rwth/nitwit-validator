#include <stdio.h>

int func(char x){
    printf("%c\n", x);
    return x;
}

int func2(char y) {
    printf("%c\n", y);
    return y;
}

int main()
{
    int (*y)(char) = &func2;
    int (*x)(char);
    x = &func;
//    x('F');
//    (*x)('f');
    return 0;
}

