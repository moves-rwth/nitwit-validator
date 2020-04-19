extern void __VERIFIER_error();
extern _Bool __VERIFIER_nondet_bool();

int main() {
    int x = 2;
    while (x > 0) {
        if (__VERIFIER_nondet_bool())
            x = x - 1; if (x == -3) x = 2;
        else
            x = x + 1; if (x == 3) x = -2;
    }
    if (x < 0) {
        __VERIFIER_error();
    }
    return 0;
}
