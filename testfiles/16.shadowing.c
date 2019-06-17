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

int main() {
    int i = 7;
    double d = 2.1;
    float f = __VERIFIER_nondet_float();

    {
        int i = 1;
        i = 230920;
        float f = d * i;

    }

    if (f == 27.0 && i == 7)
        __VERIFIER_error();

    return 0;
}