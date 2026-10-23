// Unions: members share storage (little endian), type punning through a union, initialization of the first member.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

union Word {
    unsigned int i;
    unsigned char b[4];
    float f;
};

union Wide {
    long long ll;
    int halves[2];
};

struct Tagged {
    int kind;
    union {
        int number;
        char text[8];
    } as;
};

int main(void) {
    union Word w;
    w.i = 0x01020304;
    printf("%u %u %u %u\n", w.b[0], w.b[1], w.b[2], w.b[3]);
    w.f = 1.0f;
    printf("%x\n", w.i);
    w.f = -2.5f;
    printf("%x\n", w.i);

    union Word first = {5};
    union Word bytes = {.b = {1, 2, 0, 0}};
    printf("%u %u\n", first.i, bytes.i);

    union Wide wide;
    wide.ll = -2;
    printf("%d %d\n", wide.halves[0], wide.halves[1]);
    wide.halves[1] = 1;
    printf("%lld\n", wide.ll);

    int k = __VERIFIER_nondet_int();
    w.i = 0;
    w.b[1] = (unsigned char)k;
    printf("%u\n", w.i);

    union Word copy = w;
    copy.b[0] = 0xFF;
    printf("%u %u\n", w.i, copy.i);

    struct Tagged t = {1, {.number = 42}};
    printf("%d %d\n", t.kind, t.as.number);
    t.kind = 2;
    t.as.text[0] = 'h';
    t.as.text[1] = 'i';
    t.as.text[2] = '\0';
    printf("%d %s\n", t.kind, t.as.text);
    printf("%u %u %u\n", (unsigned)sizeof(union Word), (unsigned)sizeof(union Wide), (unsigned)sizeof(struct Tagged));
    return 0;
}
