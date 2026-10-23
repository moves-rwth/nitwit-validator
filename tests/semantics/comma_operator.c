// The comma operator (C17 6.5.17): left-to-right evaluation with a sequence point, the value and type of the right
// operand.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    int a = 1, b = 2;
    int x = (a++, b++, a + b);
    printf("%d %d %d\n", a, b, x);

    int i, j;
    for (i = 0, j = 10; i < j; i += 2, j -= 3) {
        printf("(%d,%d)", i, j);
    }
    printf("\n");

    int n = 0;
    while (n++, n < 4) {
        printf("n=%d ", n);
    }
    printf("\n");

    // The result has the type of the right operand.
    char c = 'c';
    printf("%u %u\n", (unsigned)sizeof(c, 1.0), (unsigned)sizeof(1.0, c));
    double d = (1, 2.5);
    printf("%g\n", d);

    int values[5] = {10, 20, 30, 40, 50};
    printf("%d\n", values[(a = 1, a + 2)]);

    int k = __VERIFIER_nondet_int();
    int y = (k *= 2, k += 1, k * k);
    printf("%d %d\n", k, y);
    // A comma inside parentheses in an argument list is the operator; the function gets two arguments.
    printf("%d %d\n", (k, 7), 8);
    int z = 0;
    z = (z += 5, z * 3), z += 1;
    printf("%d\n", z);
    return 0;
}
