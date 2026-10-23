// The input contract (tools/difftest/README.md, "Harness"): __VERIFIER_assume with a false argument ends the run with
// the marker "NITWIT-DIFFTEST: assume" and exit code 98.
int printf(const char *, ...);
void __VERIFIER_assume(int);
int __VERIFIER_nondet_int(void);

int main(void) {
    for (int i = 0; i < 5; ++i) {
        int k = __VERIFIER_nondet_int();
        __VERIFIER_assume(k < 10);
        printf("k = %d\n", k);
    }
    printf("not reached\n");
    return 0;
}
