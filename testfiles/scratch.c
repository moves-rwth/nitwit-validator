#include <stdio.h>

int main() {
    int n = 0;
    if (1) {
        n = 1;
    } else
        _L___2: /* CIL Label */
        if (1) {
            n = 2;
        }

    printf("n: %d\n", n);
    return 0;
}