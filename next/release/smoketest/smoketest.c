extern int __VERIFIER_nondet_int(void);
void reach_error() {}

int main() {
    int x = __VERIFIER_nondet_int();
    if (x == 42) {
        reach_error();
    }
    return 0;
}
