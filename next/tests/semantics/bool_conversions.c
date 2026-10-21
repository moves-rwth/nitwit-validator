// _Bool (C17 6.3.1.2): any nonzero scalar converts to 1, also floating-point values and pointers; increments and
// decrements; results of comparisons and logical operators are int 0 or 1.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);
_Bool __VERIFIER_nondet_bool(void);

int main(void) {
    _Bool a = 5;
    _Bool b = 0.5;
    _Bool c = 0.0;
    _Bool d = -0.0;
    _Bool e = 256;
    printf("%d %d %d %d %d\n", a, b, c, d, e);
    int x = 3;
    _Bool fromPointer = &x;
    int *null = 0;
    _Bool fromNull = null;
    printf("%d %d\n", fromPointer, fromNull);

    _Bool flag = 0;
    flag++;
    printf("%d ", flag);
    flag++;
    printf("%d ", flag);
    flag--;
    printf("%d ", flag);
    flag--;
    printf("%d\n", flag);

    printf("%u %d %d\n", (unsigned)sizeof(_Bool), a + a, !a);
    printf("%d %d %d %d\n", x > 2, x == 4, !!x, x && 0);
    printf("%u %u\n", (unsigned)sizeof(x > 2), (unsigned)sizeof(!x));
    _Bool sum = a + e;
    _Bool difference = a - e;
    printf("%d %d\n", sum, difference);

    int k = __VERIFIER_nondet_int();
    _Bool fromInput = k;
    _Bool nondet = __VERIFIER_nondet_bool();
    _Bool defaulted = __VERIFIER_nondet_bool(); // the inputs are exhausted: false
    printf("%d %d %d %d\n", k, fromInput, nondet, defaulted);
    unsigned char wrapped = (unsigned char)k;
    _Bool fromWrapped = wrapped;
    printf("%d %d\n", wrapped, fromWrapped);
    return 0;
}
