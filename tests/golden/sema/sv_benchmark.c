// A program in the style of sv-benchmarks.
extern void abort(void);
extern void __assert_fail(const char *, const char *, unsigned int, const char *)
    __attribute__((__nothrow__, __leaf__)) __attribute__((__noreturn__));
void reach_error() { __assert_fail("0", "example.c", 3, "reach_error"); }
extern int __VERIFIER_nondet_int(void);
extern unsigned char __VERIFIER_nondet_uchar(void);
void __VERIFIER_assert(int cond) {
    if (!(cond)) {
    ERROR: {
        reach_error();
        abort();
    }
    }
    return;
}

int main() {
    int n = __VERIFIER_nondet_int();
    unsigned char k = __VERIFIER_nondet_uchar();
    if (n < 0 || n > 1000)
        return 0;
    int sum = 0;
    for (int i = 1; i <= n; i++)
        sum += i * k;
    __VERIFIER_assert(2 * sum == n * (n + 1) * k);
    return 0;
}
