// The input contract (tools/difftest/README.md, "Contract with the interpreter"): exit ends the run with its argument
// as the exit status, also when called from a nested function.
int printf(const char *, ...);
void exit(int);
int __VERIFIER_nondet_int(void);

int check(int value) {
    if (value > 2) {
        printf("exiting with 3\n");
        exit(3);
    }
    return value * 10;
}

int main(void) {
    for (int i = 0; i < 5; ++i) {
        printf("%d\n", check(__VERIFIER_nondet_int()));
    }
    printf("not reached\n");
    return 0;
}
