// The usual arithmetic conversions (C17 6.3.1.8): signed and unsigned operands, ILP32 vs. LP64.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    int i = -1;
    unsigned u = 1;
    long l = -1;
    unsigned long ul = 1;
    long long ll = -1;

    // int vs. unsigned: the int operand becomes unsigned.
    printf("%d %u %u\n", i < u, i + u, u - 2);
    // long vs. unsigned: long converts to unsigned long on ILP32, unsigned to long on LP64.
    printf("%d %u\n", l < u, (unsigned)sizeof(l + u));
    // long long vs. unsigned long: unsigned long converts to long long on ILP32, both to unsigned long on LP64.
    printf("%d %u\n", ll < ul, (unsigned)sizeof(ll + ul));
    printf("%d\n", ll < u);

    printf("%u %d %d\n", -1 / 2u, -7 / 2, -7 % 2);
    short s = -1;
    unsigned short us = 1;
    char c = -1;
    unsigned char uc = 255;
    printf("%d %d %d\n", s < us, c == uc, (unsigned char)200 + (signed char)-100);
    printf("%lld %u\n", (long long)u - 2, (unsigned)sizeof(c + uc));

    int x = __VERIFIER_nondet_int();
    float f = x / 2;
    double d = x / 2.0;
    printf("%f %f %f\n", f, d, x / 2.0f);
    printf("%u %u\n", (unsigned)sizeof(x + 1.0f), (unsigned)sizeof(x + 1.0));
    printf("%llu\n", (1ull << 40) + x);
    printf("%d\n", x - 5u > 0);
    printf("%ld\n", x - 5L);
    printf("%f\n", (x - 5u) * 1.0);
    int y = __VERIFIER_nondet_int();
    printf("%d %d %d\n", y < 0u, y < 0, (long long)y < 0u);
    return 0;
}
