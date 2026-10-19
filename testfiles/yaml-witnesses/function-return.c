extern int __VERIFIER_nondet_int(void);
extern unsigned char __VERIFIER_nondet_uchar(void);
void reach_error() {}

int main() {
    int a = __VERIFIER_nondet_int();
    int b = __VERIFIER_nondet_int(); int c = __VERIFIER_nondet_int();
    unsigned char d = __VERIFIER_nondet_uchar();
    if (a > 5 && b != 0 && c < -3 && d == 255) {
        reach_error();
    }
    return 0;
}
