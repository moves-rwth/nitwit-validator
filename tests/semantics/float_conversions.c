// Conversions between floating and integer types (C17 6.3.1.4, 6.3.1.5): truncation toward zero, rounding to
// nearest even, unsigned and 64-bit integers, float <-> double. Only values in range are converted.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    printf("%d %d %d %d\n", (int)2.7, (int)-2.7, (int)0.999999, (int)-0.5);
    printf("%u %lld %llu\n", (unsigned)3.9e9, (long long)-9.2e18, (unsigned long long)1.8e19);
    printf("%d %d\n", (unsigned char)255.9, (signed char)-128.9);

    // int -> float rounds to nearest, ties to even.
    printf("%.1f %.1f %.1f\n", (float)16777217, (float)16777219, (float)-16777217);
    printf("%.1f %.1f\n", (double)9007199254740993LL, (double)(1ULL << 63) + 1);
    printf("%.1f\n", (double)18446744073709551615ULL);
    printf("%.1f %.1f\n", (float)4294967295u, (double)4294967295u);

    // double -> float rounds, float -> double is exact.
    double d = 0.1;
    float f = (float)d;
    printf("%.17g %.17g\n", d, (double)f);
    printf("%a %a\n", (float)1.00000005960464477539, (float)1.00000017881393432617);

    int k = __VERIFIER_nondet_int();
    double fraction = k / 4.0;
    int back = (int)fraction;
    long long wide = (long long)(fraction * 1e12);
    printf("%f %d %lld\n", fraction, back, wide);
    float fk = (float)k;
    printf("%.1f %d\n", fk * fk, (int)(fk * fk) == k * k);
    unsigned u = (unsigned)(-k * 1000000.0);
    printf("%u\n", u);

    // Integer arithmetic in conversions: the integer is converted first, then the operation happens in floating point.
    int i = 7;
    printf("%f %f %d\n", i / 2 * 1.0, i * 1.0 / 2, (int)(i * 0.5) * 2);
    char c = (char)(k * 10.5);
    printf("%d\n", c);
    return 0;
}
