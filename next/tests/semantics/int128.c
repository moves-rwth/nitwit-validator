// __int128 (64-bit targets only): arithmetic, conversions, shifts and comparisons. Without __int128 (i386), the
// program prints a single line, so that the expected outputs of the data models differ.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

#ifdef __SIZEOF_INT128__
void print(unsigned __int128 value) {
    printf("%016llx%016llx\n", (unsigned long long)(value >> 64), (unsigned long long)value);
}

int main(void) {
    unsigned __int128 one = 1;
    print(one << 100);
    print((one << 64) - 1);
    __int128 negative = -1;
    print((unsigned __int128)negative);
    printf("%d %d\n", negative < 0, (unsigned __int128)negative > one);

    unsigned long long max = 18446744073709551615ULL;
    unsigned __int128 square = (unsigned __int128)max * max;
    print(square);
    printf("%llu %llu\n", (unsigned long long)(square >> 64), (unsigned long long)square);

    __int128 big = (__int128)1 << 100;
    __int128 quotient = big / -3;
    __int128 remainder = big % -3;
    print((unsigned __int128)quotient);
    printf("%lld\n", (long long)remainder);
    printf("%lld\n", (long long)(big >> 90));
    printf("%lld\n", (long long)(-big >> 98));

    int k = __VERIFIER_nondet_int();
    unsigned __int128 shifted = (unsigned __int128)k << (64 + k);
    print(shifted);
    printf("%u %u\n", (unsigned)sizeof(__int128), (unsigned)_Alignof(__int128));
    printf("%.1f\n", (double)big);
    return 0;
}
#else
int main(void) {
    int k = __VERIFIER_nondet_int();
    printf("no __int128 (%d)\n", k);
    return 0;
}
#endif
