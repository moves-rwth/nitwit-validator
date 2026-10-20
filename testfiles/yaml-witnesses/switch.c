extern int __VERIFIER_nondet_int(void);
extern char __VERIFIER_nondet_char(void);
void reach_error() {}

int main() {
    int a = __VERIFIER_nondet_int();
    char b = __VERIFIER_nondet_char();
    switch (a) {
    case 1:
        return 0;
    case 2:
        switch (b) {
        case 3:
            reach_error();
            break;
        default:
            return 0;
        }
        break;
    default:
        return 0;
    }
    return 0;
}
