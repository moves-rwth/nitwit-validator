// Pointer arithmetic and comparisons (C17 6.5.6, 6.5.8, 6.5.9), pointer differences, pointers to arrays.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct Pair {
    int x;
    double y;
};

int main(void) {
    int a[10];
    for (int i = 0; i < 10; ++i) {
        a[i] = i * i;
    }
    int *p = a + 3;
    printf("%d %d %d\n", *p, p[2], *(p - 1));
    printf("%d %d\n", (int)(&a[9] - p), (int)(p - &a[9]));
    printf("%d %d %d %d\n", p < a + 5, p == &a[3], p > a, p != a);
    printf("%d\n", (int)((char *)p - (char *)a));

    // A pointer one past the end may be formed and compared, but not dereferenced.
    int sum = 0;
    for (int *q = a; q != a + 10; ++q) {
        sum += *q;
    }
    printf("%d\n", sum);

    int **pp = &p;
    **pp = 42;
    *pp += 2;
    printf("%d %d\n", a[3], *p);

    struct Pair pairs[3];
    printf("%d %d\n", (int)((char *)&pairs[2] - (char *)&pairs[0]), (int)(&pairs[2] - &pairs[0]));
    printf("%u %u\n", (unsigned)sizeof(&pairs[2] - &pairs[0]), (unsigned)sizeof(p));

    int matrix[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            matrix[i][j] = 10 * i + j;
        }
    }
    int (*row)[4] = matrix;
    printf("%d %d %d\n", (*(row + 1))[2], row[2][3], (int)((char *)(row + 2) - (char *)row));
    int *inner = matrix[1];
    printf("%d %d\n", inner[3], *(inner + 1));

    int k = __VERIFIER_nondet_int();
    int *r = a + k;
    printf("%d %d %d\n", *r, r[-k], (int)(r - a));
    char *bytes = (char *)&a[1];
    printf("%d\n", (int)(bytes - (char *)a));
    int *null = 0;
    printf("%d %d\n", null == 0, p != null);
    printf("%d\n", (int *)(unsigned long)p == p);
    return 0;
}
