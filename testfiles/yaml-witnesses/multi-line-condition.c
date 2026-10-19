extern int __VERIFIER_nondet_int(void);
void reach_error() {}

int main() {
    int x = __VERIFIER_nondet_int();
    int y = __VERIFIER_nondet_int();
    int z = (x == 3) ? y : 0;
    if (x == 3 &&
        z == 4) {
        reach_error();
    }
    return 0;
}
