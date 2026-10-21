// Shift operators (C17 6.5.7): promotion of the left operand, result type, right shifts of negative values.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    unsigned u = 0x80000001u;
    printf("%u %u\n", u >> 1, u << 1);
    printf("%d %d\n", 1 << 30, 0x40000000 >> 30);
    unsigned long long top = 1ull << 63;
    printf("%llu %llu\n", top, top >> 63);
    printf("%ld %lu\n", 1L << 20, 1UL << 31);

    // Right shifts of negative values are implementation-defined; GCC and Clang shift arithmetically.
    int negative = -16;
    printf("%d %lld %d\n", negative >> 2, -1LL >> 40, -1 >> 31);
    printf("%u\n", (unsigned)-1 >> 28);

    // The left operand is promoted; the result has its promoted type, not the type of the right operand.
    unsigned char c = 0x81;
    short s = 1;
    printf("%d %d %d\n", c << 1, (unsigned char)(c << 1), s << 20);
    printf("%u %u\n", (unsigned)sizeof(1 << 2ull), (unsigned)sizeof(1ull << 2));

    int k = __VERIFIER_nondet_int();
    printf("%d %d %u\n", 1 << k, 1000 >> k, 0xFFFFFFFFu >> k);
    unsigned long long wide = 0xDEADBEEFCAFEBABEull;
    printf("%llx %llx\n", wide >> (k * 7), wide << (k * 7));
    int j = __VERIFIER_nondet_int();
    printf("%x %x\n", 0x12345678 >> j, 0x12345678u << j);
    return 0;
}
