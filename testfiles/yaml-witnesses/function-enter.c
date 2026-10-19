extern int __VERIFIER_nondet_int(void);
void reach_error() {}

int check(int x) {
    return x > 10;
}

int main() {
    int x = __VERIFIER_nondet_int();
    while (x < 12) {
        x++;
    }
    if (check(x)) {
        reach_error();
    }
    return 0;
}
