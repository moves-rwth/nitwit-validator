// The conditional operator (C17 6.5.15): only one of the second and third operands is evaluated; the result type
// follows the usual arithmetic conversions, or the composite pointer type.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct Pair {
    int a;
    int b;
};

int noisy(int value) {
    printf("noisy(%d) ", value);
    return value;
}

int main(void) {
    int t = __VERIFIER_nondet_int();
    int f = __VERIFIER_nondet_int();

    printf("%f %f\n", t ? 1 : 2.0, f ? 1 : 2.0);
    printf("%u %u\n", t ? -1 : 1u, f ? 1u : -1);
    printf("%llu\n", t ? -1 : 1ULL);
    printf("%u %u %u\n", (unsigned)sizeof(t ? 'a' : 1LL), (unsigned)sizeof(t ? (char)1 : (short)2),
           (unsigned)sizeof(t ? 1.0f : 2));
    printf("%d\n", t ? (unsigned char)200 : (signed char)-1);
    printf("%d\n", (f ? (unsigned char)200 : (signed char)-1) < 0);
    printf("%d\n", noisy(t) ? noisy(10) : noisy(20));
    printf("%d\n", noisy(f) ? noisy(10) : noisy(20));

    int x = 5, y = 7;
    int *pointer = t ? &x : &y;
    *pointer += 100;
    printf("%d %d\n", x, y);
    void *opaque = &y;
    int *selected = f ? opaque : &x; // void * and int *: the result is void *
    printf("%d\n", *selected);
    int *maybeNull = f ? &x : 0;
    printf("%d\n", maybeNull == 0);

    struct Pair p = {1, 2}, q = {3, 4};
    struct Pair chosen = t ? q : p;
    printf("%d %d\n", chosen.a, chosen.b);

    int n = __VERIFIER_nondet_int();
    const char *size = n < 10 ? "small" : n < 100 ? "medium" : "large";
    printf("%s\n", size);
    int clamped = n < 0 ? 0 : n > 50 ? 50 : n;
    printf("%d\n", clamped);
    *(t ? &x : &y) = 0; // the result is not an lvalue in C, so assign through a pointer
    printf("%d %d\n", x, y);
    return 0;
}
