extern void __VERIFIER_error();
extern void __VERIFIER_assume(int);

int one(int foo) {
    return foo;
}

int x = 0;

void step() {
    x = one(1);
}

int main() {
    step();
    __VERIFIER_assume(x == 1);
    if (x == 1) {
        __VERIFIER_error();
    }
    return 0;
}

