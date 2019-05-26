#include <stdio.h>

int main() {
    int a, b;
    int *p1, *p2;

    p1 = &a;
    p2 = NULL;

    b = 1;
    a = 5;

    printf("%d\n", *p1);

    return 0;
}