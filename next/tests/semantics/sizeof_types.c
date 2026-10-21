// sizeof and _Alignof of the basic types in ILP32 and LP64; sizeof does not evaluate its operand (unless it is a
// VLA).
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct Opaque;

int main(void) {
    printf("char %u short %u int %u long %u long long %u\n", (unsigned)sizeof(char), (unsigned)sizeof(short),
           (unsigned)sizeof(int), (unsigned)sizeof(long), (unsigned)sizeof(long long));
    printf("float %u double %u long double %u\n", (unsigned)sizeof(float), (unsigned)sizeof(double),
           (unsigned)sizeof(long double));
    printf("pointer %u function pointer %u struct pointer %u\n", (unsigned)sizeof(void *),
           (unsigned)sizeof(int (*)(void)), (unsigned)sizeof(struct Opaque *));
    printf("size_t %u ptrdiff_t %u\n", (unsigned)sizeof(sizeof(int)), (unsigned)sizeof((char *)0 - (char *)0));
    printf("align: short %u int %u long %u long long %u double %u long double %u pointer %u\n",
           (unsigned)_Alignof(short), (unsigned)_Alignof(int), (unsigned)_Alignof(long), (unsigned)_Alignof(long long),
           (unsigned)_Alignof(double), (unsigned)_Alignof(long double), (unsigned)_Alignof(void *));

    int i = __VERIFIER_nondet_int();
    unsigned size = (unsigned)sizeof(i++);
    printf("%u %d\n", size, i);
    size = (unsigned)sizeof(i = 100);
    printf("%u %d\n", size, i);
    double values[7];
    printf("%u %u %u\n", (unsigned)sizeof values, (unsigned)sizeof values[0], (unsigned)sizeof(values + 0));
    printf("%u %u %u\n", (unsigned)sizeof "text", (unsigned)sizeof(char[3][5]), (unsigned)sizeof(int *[4]));
    // The result of sizeof is unsigned: sizeof(int) - 5 wraps around.
    printf("%d\n", sizeof(int) - 5 > 0);
    printf("%u\n", (unsigned)sizeof(sizeof(int) + i));
    return 0;
}
