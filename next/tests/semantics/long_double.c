// long double: x87 80-bit extended precision on both data models; its size and alignment differ (12/4 vs. 16/16).
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    printf("%u %u\n", (unsigned)sizeof(long double), (unsigned)_Alignof(long double));
    long double third = 1.0L / 3;
    printf("%.21Lg\n", third);
    printf("%La\n", third);
    printf("%.17g\n", (double)third);

    // 64 significand bits: every long long is exact.
    long double exact = (long double)9223372036854775807LL;
    printf("%.0Lf %d\n", exact, (long long)exact == 9223372036854775807LL);
    printf("%d %d\n", 1.0L + 0x1p-63L != 1.0L, 1.0L + 0x1p-64L != 1.0L);
    printf("%d\n", (double)(1.0L + 0x1p-60L) == 1.0);

    long double big = 1e4000L;
    printf("%Lg %Lg\n", big, big * big);
    printf("%Le\n", 1.0L / 7);

    int k = __VERIFIER_nondet_int();
    long double product = 1;
    for (int i = 1; i <= k; ++i) {
        product *= i;
    }
    printf("%.0Lf\n", product);
    long double mixed = k / 3.0L + 0.5f;
    printf("%.20Lf\n", mixed);
    unsigned long long wide = (unsigned long long)(product / 1000);
    printf("%llu\n", wide);
    return 0;
}
