#include <stdio.h>

int main() {
    int a, b;
    int *p1, *p2;

    p1 = &a;
    p2 = &b;

    b = 1;
    a = 5;

    printf("%d\n", *p1);
    printf("%d\n", *p2);

    return 0;
}