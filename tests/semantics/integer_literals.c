// Integer and character constants and their types (C17 6.4.4.1, 6.4.4.4). An expression x - x - 1 < 0 tells whether
// the type of x is signed.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    printf("%d %d %d %d\n", 42, 052, 0x2a, 0X2A);
    printf("%u %u %u %u\n", (unsigned)sizeof(2147483647), (unsigned)sizeof(2147483648), (unsigned)sizeof(4294967295),
           (unsigned)sizeof(4294967296));
    // Decimal constants without suffix are int, long, long long; octal and hexadecimal ones may be unsigned.
    printf("%d %d %d\n", 2147483647 - 2147483647 - 1 < 0, 2147483648 - 2147483648 - 1 < 0,
           4294967295 - 4294967295 - 1 < 0);
    printf("%d %d %d\n", 0x7FFFFFFF - 0x7FFFFFFF - 1 < 0, 0x80000000 - 0x80000000 - 1 < 0,
           0xFFFFFFFF - 0xFFFFFFFF - 1 < 0);
    printf("%u %d\n", (unsigned)sizeof(0x80000000), 020000000000 - 020000000000 - 1 < 0);
    printf("%u %d\n", (unsigned)sizeof(0xFFFFFFFFFFFFFFFF), 0xFFFFFFFFFFFFFFFF - 0xFFFFFFFFFFFFFFFF - 1 < 0);
    printf("%u %d\n", (unsigned)sizeof(0x7FFFFFFFFFFFFFFF), 0x7FFFFFFFFFFFFFFF - 0x7FFFFFFFFFFFFFFF - 1 < 0);

    printf("%u %u %u %u %u\n", (unsigned)sizeof(1u), (unsigned)sizeof(1l), (unsigned)sizeof(1ul), (unsigned)sizeof(1ll),
           (unsigned)sizeof(1ULL));
    printf("%d %d %d\n", 1u - 2 > 0, 1l - 2 < 0, 1lu - 2 > 0);
    printf("%d %d\n", 4294967295u + 1 == 0, 4294967295 + 1 == 4294967296);
    printf("%lld %llu\n", 9223372036854775807LL, 18446744073709551615ULL);
    printf("%d\n", -2147483648 < 0);
    printf("%u\n", (unsigned)sizeof(-2147483648));

    // Character constants have type int.
    printf("%u %d %d %d %d %d\n", (unsigned)sizeof('a'), 'a', '\n', '\x41', '\101', '\0');
    printf("%d %d %d %d\n", '\\', '\'', '"', '\t');
    printf("%d %d\n", '\xff', '\377');

    int k = __VERIFIER_nondet_int();
    printf("%lld\n", k * 1000000000000LL);
    printf("%u\n", k * 0x10000000u);
    printf("%d\n", k + 0xA);
    return 0;
}
