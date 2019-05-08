extern void __VERIFIER_error() ;
extern void exit(int);

int f(int n) {
    if (n < 3) return n;
    else __VERIFIER_error();
    return (1);
}

int main() {
    return f(3);
}

