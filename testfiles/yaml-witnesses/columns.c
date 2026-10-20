extern int __VERIFIER_nondet_int(void);
void reach_error() {}

int main() {
    int a = __VERIFIER_nondet_int(); int b = __VERIFIER_nondet_int();
    if (b == 7 && a != 7) {
        reach_error();
    }
    return 0;
}
