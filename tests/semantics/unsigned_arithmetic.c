// Unsigned arithmetic wraps modulo 2^N (C17 6.2.5p9); narrow unsigned types promote to int first, so their
// arithmetic only wraps when the result is stored back.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    unsigned u = 4294967295u;
    printf("%u %u %u\n", u + 1, 0u - 1, u * 2);
    unsigned long long ull = 18446744073709551615ULL;
    printf("%llu %llu\n", ull + 2, ull * ull);
    unsigned long ul = 0;
    ul -= 1;
    printf("%lu\n", ul);

    unsigned char uc = 250;
    uc += 10;
    printf("%d %d\n", uc, (unsigned char)250 + 10);
    unsigned short us = 65535;
    us++;
    printf("%d\n", us);
    // 65535 * 65535 would overflow int after promotion; multiply as unsigned instead.
    unsigned short big = 65535;
    printf("%u\n", (unsigned)big * big);

    unsigned negated = -(unsigned)5;
    printf("%u %d\n", negated, (int)negated);
    printf("%u\n", ~0u >> 1);

    int k = __VERIFIER_nondet_int();
    unsigned counter = (unsigned)k;
    unsigned steps = 0;
    while (counter != 0) {
        counter += 1000000000u;
        ++steps;
    }
    printf("%u\n", steps);
    unsigned hash = 2166136261u;
    for (int i = 0; i < 10; ++i) {
        hash = (hash ^ (unsigned)(k + i)) * 16777619u;
    }
    printf("%u\n", hash);
    unsigned long long h64 = 14695981039346656037ULL;
    for (int i = 0; i < 10; ++i) {
        h64 = (h64 ^ ((unsigned long long)k * i)) * 1099511628211ULL;
    }
    printf("%llu\n", h64);
    return 0;
}
