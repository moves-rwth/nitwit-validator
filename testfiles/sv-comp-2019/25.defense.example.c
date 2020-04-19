extern void  __VERIFIER_error();
extern void  __VERIFIER_assume();
extern int   __VERIFIER_nondet_int();
extern _Bool __VERIFIER_nondet_bool();
void __VERIFIER_assert(int cond) {
    if (!cond) __VERIFIER_error();
}

int main() {
    int x = __VERIFIER_nondet_int();
    __VERIFIER_assume(x >= 0);
    while (x > 0) {
        _Bool b = __VERIFIER_nondet_bool();
        if (b)
            x = x - 1;
        else
            x = x + 1;
    }
    __VERIFIER_assert(x >= 0);
    return 0;
}
