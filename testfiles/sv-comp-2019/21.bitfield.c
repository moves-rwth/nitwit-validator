extern void __VERIFIER_error() __attribute__ ((__noreturn__));
extern void __VERIFIER_assume(int);
void __VERIFIER_assert(int cond) { if(!(cond)) { ERROR: __VERIFIER_error(); } }
extern int __VERIFIER_nondet_int(void);

struct bf {
    int a: 1;
    int b: 2;
    int c: 3;
    int d : 4, e: 4;
};

int main ()
{
    struct bf s;
    struct bf s2;
    s.a = 9;
    s.b = 9;
    s.c = 9;
    s.d = 9;
    s.e = 9;
    printf("%d, %d, %d, %d, %d\n", s.a, s.b, s.c, s.d, s.e);
    s.a += 1;
    s.b += 1;
    s.c += 1;
    s.d += 1;
    s.e += 1;
    printf("%d, %d, %d, %d, %d\n", s.a, s.b, s.c, s.d, s.e);
    s.a += -2;
    s.b += -2;
    s.c += -2;
    s.d += -2;
    s.e += -2;
    printf("%d, %d, %d, %d, %d\n", s.a, s.b, s.c, s.d, s.e);
    s2 = s;
    printf("%d, %d, %d, %d, %d\n", s2.a, s2.b, s2.c, s2.d, s2.e);
    if (s2.a == 0 && s2.b == 0 && s2.c == 0 && s2.d == -8 && s2.e == -8){
        __VERIFIER_error();
    }
    return 0;
}

