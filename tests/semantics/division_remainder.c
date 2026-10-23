// Integer division truncates toward zero and (a/b)*b + a%b == a (C17 6.5.5p6), for all sign combinations and widths.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    int dividends[] = {7, -7, 7, -7, 0, 1, -1};
    int divisors[] = {2, 2, -2, -2, 3, 5, 5};
    for (int i = 0; i < 7; ++i) {
        int a = dividends[i], b = divisors[i];
        printf("%d/%d=%d %d%%%d=%d check=%d\n", a, b, a / b, a, b, a % b, (a / b) * b + a % b == a);
    }
    printf("%lld %lld\n", -9223372036854775807LL / 10, -9223372036854775807LL % 10);
    printf("%u %u\n", 4294967295u / 7, 4294967295u % 7);
    printf("%llu %llu\n", 18446744073709551615ULL / 3, 18446744073709551615ULL % 1000);
    printf("%d %d\n", -2147483647 / 2, -2147483647 % 2);
    // Mixed signedness: -7 becomes unsigned.
    printf("%u %u\n", -7 / 2u, -7 % 2u);
    printf("%ld %ld\n", -7L / 2, -7L % 2);
    short s = -100;
    unsigned char uc = 7;
    printf("%d %d\n", s / uc, s % uc);

    int k = __VERIFIER_nondet_int();
    int m = __VERIFIER_nondet_int();
    printf("%d %d %d %d\n", k / m, k % m, -k / m, k % -m);
    long long wide = (long long)k * 1000000007;
    printf("%lld %lld\n", wide / m, wide % m);
    unsigned u = (unsigned)k;
    printf("%u %u\n", u / (unsigned)m, u % (unsigned)m);
    return 0;
}
