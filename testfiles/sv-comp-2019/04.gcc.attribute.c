extern void __VERIFIER_error() __attribute__ ((__noreturn__));

int f2(int n) {
    if (n < 3) return n;
    f(--n);
    __VERIFIER_error();
    return 0;
}

int f(int n) {
    if (n < 3) return n;
    f2(--n);
    __VERIFIER_error();
    return 0;
}

int main() {
    return f(7);
}

