// Short-circuit evaluation (C17 6.5.13, 6.5.14): the right operand is evaluated only when needed, and there is a
// sequence point after the left operand.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int calls = 0;

int trace(int value) {
    ++calls;
    printf("trace(%d) ", value);
    return value;
}

int main(void) {
    int r = trace(0) && trace(1);
    printf("-> %d\n", r);
    r = trace(1) && trace(2);
    printf("-> %d\n", r);
    r = trace(1) || trace(2);
    printf("-> %d\n", r);
    r = trace(0) || trace(0);
    printf("-> %d\n", r);
    r = trace(0) && trace(1) || trace(2);
    printf("-> %d\n", r);
    r = trace(1) || trace(0) && trace(3);
    printf("-> %d\n", r);
    printf("calls %d\n", calls);

    int *null = 0;
    int value = 5;
    int *pointer = &value;
    printf("%d %d\n", null && *null > 0, pointer && *pointer > 0);

    // The side effect of the left operand is complete before the right operand is evaluated.
    int i = 0;
    if (i++ == 0 && i == 1) {
        printf("sequenced %d\n", i);
    }
    int j = 0;
    r = j++ || j++;
    printf("%d %d\n", r, j);

    int k = __VERIFIER_nondet_int();
    int divisor = __VERIFIER_nondet_int();
    if (divisor != 0 && k / divisor > 2) {
        printf("quotient %d\n", k / divisor);
    }
    if (divisor == 0 || k % divisor == 0) {
        printf("divisible or zero\n");
    }
    int positive = k > 0 && trace(k);
    int zeroOrNegated = !k || trace(-k);
    printf("-> %d %d\n", positive, zeroOrNegated);
    printf("%d\n", (k && divisor) + (k || divisor) + !(k && !divisor));
    return 0;
}
