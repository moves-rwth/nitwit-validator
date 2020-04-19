extern void __VERIFIER_error();

#include <stdlib.h>
#include <stdio.h>

int *getArray() {
    int *a = malloc(20 * sizeof a);
    for (int i = 0; i < 20; ++i) {
        a[i] = i;
    }
    return a;
}

int main() {
    int *x = getArray();

    for (int i = 0; i < 20; ++i) {
        if (x[i] > 18) {
            __VERIFIER_error();
        }
    }
    free(x);
    return 0;
}
