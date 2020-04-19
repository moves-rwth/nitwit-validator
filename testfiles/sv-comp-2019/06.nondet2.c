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

void main() {
    int a1 = __VERIFIER_nondet_int();
    unsigned int a2 = __VERIFIER_nondet_uint();
    unsigned short a3 = __VERIFIER_nondet_ushort();
    short a4 = __VERIFIER_nondet_short();
    long a5 = __VERIFIER_nondet_long();
    unsigned long a6 = __VERIFIER_nondet_ulong();
    char a7 = __VERIFIER_nondet_char();
    unsigned char a8 = __VERIFIER_nondet_uchar();
    double a9 = __VERIFIER_nondet_double();


    if (a1 == 1223 &&
        a2 == 13443u &&
        a3 == 12u &&
        a4 == 134 &&
        a5 == 1232444l &&
        a6 == 135345324u &&
        a7 == (char)'w' &&
        a8 == (unsigned char)'F' &&
        a9 == 8.20392
    ) {
        __VERIFIER_error();
    }
}