// Scopes and name spaces (C17 6.2.1, 6.2.3): shadowing in nested blocks, block-scope typedefs and extern
// declarations, tags, members and labels with the same name as variables.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int value = 1;
typedef int Number;

struct value {
    int value;
};

int readGlobal(void) {
    return value;
}

int main(void) {
    printf("%d\n", value);
    int value = 2;
    printf("%d %d\n", value, readGlobal());
    {
        int value = 3;
        {
            extern int value; // refers to the file-scope object
            printf("%d\n", value);
            value = 10;
        }
        printf("%d\n", value);
    }
    printf("%d %d\n", value, readGlobal());

    struct value v = {4};
    printf("%d\n", v.value);
    goto value;
value:
    printf("label\n");

    {
        typedef double Number; // shadows the file-scope typedef
        Number half = 0.5;
        printf("%g %u\n", half, (unsigned)sizeof(Number));
    }
    Number whole = 7;
    printf("%d %u\n", whole, (unsigned)sizeof(Number));

    int k = __VERIFIER_nondet_int();
    for (int k2 = k; k2 > 0; --k2) {
        int k = k2 * 10; // shadows the outer k within the loop body
        printf("%d ", k);
    }
    printf("| %d\n", k);
    int Number2 = sizeof(Number) * 2;
    printf("%d\n", Number2);
    // A declaration's scope starts after its declarator: the initializer refers to the outer value.
    {
        int outer = 5;
        {
            int outer2 = outer + 1, outer = outer2 * 2;
            printf("%d %d\n", outer2, outer);
        }
    }
    return 0;
}
