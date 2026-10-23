// The input contract (tools/difftest/README.md, "Harness"): abort ends the run by SIGABRT (exit status 134), without
// a marker; the output printed before, even without a line break, is kept.
int printf(const char *, ...);
void abort(void);
int __VERIFIER_nondet_int(void);

int main(void) {
    int k = __VERIFIER_nondet_int();
    printf("k = %d, before abort", k);
    if (k > 0) {
        abort();
    }
    printf("\nnot reached\n");
    return 0;
}
