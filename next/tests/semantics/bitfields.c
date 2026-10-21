// Bit-fields: truncation on assignment, signedness, promotion, and allocation in storage units (x86 SysV ABI).
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct Flags {
    unsigned a : 3;
    unsigned b : 5;
    int c : 4;
    unsigned d : 1;
};

struct Mixed {
    char x;
    int y : 7;
    int z : 9;
    char w;
};

struct Straddle {
    unsigned char a : 6;
    unsigned char b : 6;
    unsigned short c : 12;
    unsigned int d : 20;
};

struct Unnamed {
    int first : 3;
    int : 0;
    int second : 3;
    unsigned : 4;
    unsigned third : 2;
};

int main(void) {
    struct Flags f = {0};
    f.a = 9; // stored modulo 2^3
    f.b = 31;
    f.c = -3;
    f.d = 1;
    printf("%u %u %d %u\n", (unsigned)f.a, (unsigned)f.b, f.c, (unsigned)f.d);
    f.b += 1;
    printf("%u\n", (unsigned)f.b);
    // Bit-fields narrower than int promote to int, even the unsigned ones.
    printf("%d %d\n", f.a - 2 < 0, f.c - 10);

    int k = __VERIFIER_nondet_int();
    f.c = k;
    f.a = k;
    printf("%d %u %d\n", f.c, (unsigned)f.a, f.a + f.c);

    struct Mixed m = {'a', -5, 200, 'b'};
    printf("%c %d %d %c\n", m.x, m.y, m.z, m.w);
    m.y = 63;
    m.z = -256;
    printf("%d %d\n", m.y, m.z);

    struct Straddle s = {63, 1, 4095, 1048575};
    s.b += 62;
    printf("%u %u %u %u\n", (unsigned)s.a, (unsigned)s.b, (unsigned)s.c, (unsigned)s.d);

    struct Unnamed u = {-4, 3, 2};
    printf("%d %d %u\n", u.first, u.second, (unsigned)u.third);

    printf("%u %u %u %u\n", (unsigned)sizeof(struct Flags), (unsigned)sizeof(struct Mixed),
           (unsigned)sizeof(struct Straddle), (unsigned)sizeof(struct Unnamed));
    return 0;
}
