// The input contract (tools/difftest/README.md, "Harness"): reach_error of sv-benchmarks calls __assert_fail, which
// ends the run with the marker "NITWIT-DIFFTEST: error __assert_fail" and exit code 97.
int printf(const char *, ...);
void __assert_fail(const char *, const char *, unsigned int, const char *);
int __VERIFIER_nondet_int(void);

void reach_error(void) {
    __assert_fail("0", "contract_error.c", 8, "reach_error");
}

int main(void) {
    for (int i = 0; i < 3; ++i) {
        int k = __VERIFIER_nondet_int();
        printf("k = %d\n", k);
        if (k == 42) {
            reach_error();
        }
    }
    printf("not reached\n");
    return 0;
}
