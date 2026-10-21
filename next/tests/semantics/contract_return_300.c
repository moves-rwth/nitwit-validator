// The input contract (tools/difftest/README.md, "Contract with the interpreter"): the exit status is the value returned
// by main modulo 256, here 300 -> 44.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    int k = __VERIFIER_nondet_int();
    printf("returning %d\n", 300 + k);
    return 300 + k;
}
