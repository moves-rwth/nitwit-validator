# 1 "00.basic.c"
# 1 "<built-in>"
# 1 "<command-line>"
# 31 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3 4
# 32 "<command-line>" 2
# 1 "00.basic.c"
extern void __VERIFIER_error();

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
            __VERIFIER_error();
            return 1;
        }
    }
}
