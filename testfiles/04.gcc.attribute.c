extern void __VERIFIER_error() __attribute__ ((__noreturn__));

int f2(int n) {
    if (n < 3) return;
    f(--n);
    __VERIFIER_error();
}

int f(int n) {
    if (n < 3) return;
    f2(--n);
    __VERIFIER_error();
}

int main() {
    return f(7);
}

