// Struct and union layout: sizes, alignments and member offsets for ILP32 (i386) and LP64 (x86-64).
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct A { char c; int i; };
struct B { char c; double d; };
struct C { char c; long long ll; };
struct D { short s; char c; };
struct E { char c[3]; };
struct F { char c; long double ld; };
struct G { char c; struct A a; char d; };
struct H { char *p; char c; };
struct I { int n; int items[]; };
struct J { char c; short s; char d; int i; char e; };
struct __attribute__((packed)) P { char c; int i; short s; };
struct __attribute__((aligned(16))) Q { int i; };
union U { char c; int i; double d; };

int main(void) {
    printf("A %u %u %u\n", (unsigned)sizeof(struct A), (unsigned)_Alignof(struct A),
           (unsigned)__builtin_offsetof(struct A, i));
    printf("B %u %u %u\n", (unsigned)sizeof(struct B), (unsigned)_Alignof(struct B),
           (unsigned)__builtin_offsetof(struct B, d));
    printf("C %u %u %u\n", (unsigned)sizeof(struct C), (unsigned)_Alignof(struct C),
           (unsigned)__builtin_offsetof(struct C, ll));
    printf("D %u %u\n", (unsigned)sizeof(struct D), (unsigned)_Alignof(struct D));
    printf("E %u %u\n", (unsigned)sizeof(struct E), (unsigned)_Alignof(struct E));
    printf("F %u %u %u\n", (unsigned)sizeof(struct F), (unsigned)_Alignof(struct F),
           (unsigned)__builtin_offsetof(struct F, ld));
    printf("G %u %u %u\n", (unsigned)sizeof(struct G), (unsigned)__builtin_offsetof(struct G, a),
           (unsigned)__builtin_offsetof(struct G, d));
    printf("H %u %u\n", (unsigned)sizeof(struct H), (unsigned)__builtin_offsetof(struct H, c));
    printf("I %u %u\n", (unsigned)sizeof(struct I), (unsigned)__builtin_offsetof(struct I, items));
    printf("J %u %u %u %u %u\n", (unsigned)sizeof(struct J), (unsigned)__builtin_offsetof(struct J, s),
           (unsigned)__builtin_offsetof(struct J, d), (unsigned)__builtin_offsetof(struct J, i),
           (unsigned)__builtin_offsetof(struct J, e));
    printf("P %u %u %u %u\n", (unsigned)sizeof(struct P), (unsigned)_Alignof(struct P),
           (unsigned)__builtin_offsetof(struct P, i), (unsigned)__builtin_offsetof(struct P, s));
    printf("Q %u %u\n", (unsigned)sizeof(struct Q), (unsigned)_Alignof(struct Q));
    printf("U %u %u\n", (unsigned)sizeof(union U), (unsigned)_Alignof(union U));

    // Offsets computed from addresses agree with offsetof.
    struct J items[4];
    int k = __VERIFIER_nondet_int();
    printf("%d %d\n", (int)((char *)&items[k] - (char *)items), (int)((char *)&items[k].i - (char *)&items[k]));
    struct P packed = {1, 0x01020304, 5};
    packed.i += k;
    printf("%d %d %d\n", packed.c, packed.i, packed.s);
    return 0;
}
