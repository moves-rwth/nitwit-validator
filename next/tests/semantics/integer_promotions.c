// Integer promotions (C17 6.3.1.1) and conversions between integer types (C17 6.3.1.3).
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    unsigned char uc = 200;
    signed char sc = -100;
    unsigned short us = 65000;
    short ss = -30000;

    // Operands narrower than int are promoted to int before the operation.
    printf("%d %d %d %d\n", uc + uc, sc * 2, us + us, ss - 10000);
    printf("%d %d %d\n", ~uc, -uc, !uc);
    printf("%u %u %u\n", (unsigned)sizeof(uc + uc), (unsigned)sizeof(+sc), (unsigned)sizeof(-us));
    unsigned char high = 0xF0;
    printf("%d %d\n", (high << 4) >> 4, (unsigned char)(high << 4) >> 4);

    // Conversions to unsigned types are modulo 2^N; to signed types GCC and Clang also wrap.
    printf("%d %d\n", (unsigned char)(uc + 100), (signed char)(uc + 100));
    printf("%u %d\n", (unsigned)-1, (int)4294967295u);
    printf("%d %d\n", (short)65535, (unsigned short)-1);
    printf("%lld %llu\n", (long long)-7, (unsigned long long)(unsigned)-7);
    printf("%llu\n", (unsigned long long)-7);

    char c = (char)__VERIFIER_nondet_int();
    unsigned char d = (unsigned char)__VERIFIER_nondet_int();
    short s = (short)__VERIFIER_nondet_int();
    unsigned u = (unsigned)__VERIFIER_nondet_int();
    long long ll = __VERIFIER_nondet_int();
    printf("%d %d %d %u %lld\n", c, d, s, u, ll);
    printf("%d %d %d\n", c + d, c * s, d - 256);
    printf("%d %d\n", c < d, s == -25536);
    unsigned short wide = (unsigned short)s;
    printf("%d %d\n", wide, wide + s);
    return 0;
}
