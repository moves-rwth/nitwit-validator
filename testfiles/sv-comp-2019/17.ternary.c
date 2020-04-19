extern void __VERIFIER_error() ;
extern int __VERIFIER_nondet_int();
extern unsigned int __VERIFIER_nondet_uint();
extern unsigned short __VERIFIER_nondet_ushort();
extern short __VERIFIER_nondet_short();
extern long __VERIFIER_nondet_long();
extern unsigned long __VERIFIER_nondet_ulong();
extern char __VERIFIER_nondet_char();
extern unsigned char __VERIFIER_nondet_uchar();
extern double __VERIFIER_nondet_double();
extern double __VERIFIER_nondet_float();


int blah(int i) {
    return ++i;
}

int main() {
    int i = 0;
    i = 1 ? i : blah(i);
    i = 0 ? blah(i) : i;

    if (i == 0)
        __VERIFIER_error();

    return 0;
}