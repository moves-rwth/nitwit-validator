# 1 "03.typedef.c"
# 1 "<built-in>"
# 1 "<command-line>"
# 31 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3 4
# 32 "<command-line>" 2
# 1 "03.typedef.c"
extern void __VERIFIER_error();

typedef long unsigned int size_t;

int main() {
    size_t t = 1 + 9/2 + 3 * 10;
    if (t == 35) {
        __VERIFIER_error();
        return 1;
    }
    return 0;
}
