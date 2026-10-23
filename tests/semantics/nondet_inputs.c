// The input contract (tools/difftest/README.md, "Harness"): every __VERIFIER_nondet_* function takes the next line of
// the inputs file and converts it to its return type like a cast; exhausted inputs give 0.
int printf(const char *, ...);
char __VERIFIER_nondet_char(void);
unsigned char __VERIFIER_nondet_uchar(void);
short __VERIFIER_nondet_short(void);
unsigned short __VERIFIER_nondet_ushort(void);
int __VERIFIER_nondet_int(void);
unsigned int __VERIFIER_nondet_uint(void);
long __VERIFIER_nondet_long(void);
unsigned long __VERIFIER_nondet_ulong(void);
long long __VERIFIER_nondet_longlong(void);
unsigned long long __VERIFIER_nondet_ulonglong(void);
_Bool __VERIFIER_nondet_bool(void);
float __VERIFIER_nondet_float(void);
double __VERIFIER_nondet_double(void);

int main(void) {
    char c = __VERIFIER_nondet_char();
    unsigned char uc = __VERIFIER_nondet_uchar();
    short s = __VERIFIER_nondet_short();
    unsigned short us = __VERIFIER_nondet_ushort();
    printf("%d %d %d %d\n", c, uc, s, us);

    int i = __VERIFIER_nondet_int();
    unsigned int u = __VERIFIER_nondet_uint();
    long l = __VERIFIER_nondet_long();
    unsigned long ul = __VERIFIER_nondet_ulong();
    printf("%d %u %ld %lu\n", i, u, l, ul);

    long long ll = __VERIFIER_nondet_longlong();
    unsigned long long ull = __VERIFIER_nondet_ulonglong();
    _Bool b = __VERIFIER_nondet_bool();
    printf("%lld %llu %d\n", ll, ull, b);

    float f = __VERIFIER_nondet_float();
    double d = __VERIFIER_nondet_double();
    float fromInteger = __VERIFIER_nondet_float();
    printf("%a %a %a\n", f, d, fromInteger);

    // The inputs are exhausted from here on.
    int defaulted = __VERIFIER_nondet_int();
    double defaultedDouble = __VERIFIER_nondet_double();
    printf("%d %g\n", defaulted, defaultedDouble);
    return 0;
}
