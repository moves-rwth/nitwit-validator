// Plain char is signed on x86 (implementation-defined, C17 6.2.5p15); char, signed char and unsigned char in
// arithmetic, comparisons and conversions.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int countHighBytes(const char *text) {
    int count = 0;
    for (; *text; ++text) {
        if (*text < 0) {
            ++count;
        }
    }
    return count;
}

int main(void) {
    char c = (char)200;
    signed char sc = (signed char)200;
    unsigned char uc = 200;
    printf("%d %d %d\n", c, sc, uc);
    printf("%d %d %d\n", c == sc, c == uc, (unsigned char)c == uc);
    printf("%d %d\n", c < 0, uc > 127);
    printf("%d %d\n", (char)127 + 1, (char)(127 + 1));

    char text[] = "a\xe9t\xe9";
    printf("%d\n", countHighBytes(text));
    unsigned sum = 0;
    for (int i = 0; text[i]; ++i) {
        sum += (unsigned char)text[i];
    }
    printf("%u\n", sum);

    int k = __VERIFIER_nondet_int();
    char fromInt = (char)k;
    unsigned char unsignedFromInt = (unsigned char)k;
    printf("%d %d %d\n", fromInt, unsignedFromInt, fromInt + unsignedFromInt);
    int asIndex = (unsigned char)fromInt;
    printf("%d\n", asIndex);
    char digit = '7';
    printf("%d %c\n", digit - '0', 'a' + 2);
    // char promotes to int before the conversion to unsigned, so the sign is extended.
    printf("%u %u\n", (unsigned)c, (unsigned)uc);
    printf("%x\n", (unsigned)(unsigned char)c);
    return 0;
}
