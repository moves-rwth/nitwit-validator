//extern void __VERIFIER_error();

int one(int foo) {
    return foo;
}

int x = 0;

void step() {
    x = one(1);
}

int main() {
    while (1) {
        step();
        if (x == 1) {
            printf("__VERIFIER_error();");
            return 1;
        }
    }
}

