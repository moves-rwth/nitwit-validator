// Compound assignment (C17 6.5.16.2) and increment/decrement: E1 op= E2 is E1 = E1 op E2 with E1 evaluated once, in
// the type of the usual arithmetic conversions, converted back to the type of E1.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int indexCalls = 0;

int nextIndex(void) {
    return indexCalls++;
}

int main(void) {
    char c = 100;
    c += 100; // computed in int, then converted to char
    printf("%d\n", c);
    unsigned char uc = 10;
    uc -= 20;
    printf("%d\n", uc);
    short s = 300;
    s *= 300;
    printf("%d\n", s);
    int i = 3;
    i += 0.5;
    printf("%d\n", i);
    i *= 2.9;
    printf("%d\n", i);
    unsigned u = 5;
    u -= 6;
    printf("%u\n", u);
    int x = 1000;
    x /= 7;
    printf("%d ", x);
    x %= 9;
    printf("%d ", x);
    x <<= 4;
    printf("%d ", x);
    x >>= 2;
    printf("%d ", x);
    x &= 0x5;
    printf("%d ", x);
    x |= 0x30;
    printf("%d ", x);
    x ^= 0xFF;
    printf("%d\n", x);
    double d = 1.5;
    d *= 4;
    d -= 0.25;
    printf("%g\n", d);
    float f = 0.1f;
    f += 0.2;
    printf("%.9g\n", f);

    // The left operand is evaluated once.
    int values[4] = {0, 0, 0, 0};
    values[nextIndex()] += 5;
    values[nextIndex()] += 7;
    printf("%d %d %d %d\n", values[0], values[1], values[2], indexCalls);

    int a = 5;
    int pre = ++a;
    int post = a++;
    printf("%d %d %d\n", pre, post, a);
    double dd = 0.5;
    dd++;
    --dd;
    --dd;
    printf("%g\n", dd);
    int array[3] = {10, 20, 30};
    int *p = array;
    int first = *p++;
    int second = *++p;
    int third = ++*p;
    printf("%d %d %d %d\n", first, second, third, (int)(p - array));

    int k = __VERIFIER_nondet_int();
    long long acc = 1;
    for (int j = 0; j < 5; ++j) {
        acc *= k;
        acc -= j;
    }
    printf("%lld\n", acc);
    signed char narrow = (signed char)k;
    narrow += narrow;
    printf("%d\n", narrow);
    return 0;
}
