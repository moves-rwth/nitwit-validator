// Variable length arrays: sizeof evaluated at run time, VLA parameters, VLA typedefs, VLAs in loops.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int sum(int n, int values[n]) {
    int total = 0;
    for (int i = 0; i < n; ++i) {
        total += values[i];
    }
    return total;
}

void fill(int rows, int cols, int m[rows][cols]) {
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            m[i][j] = i * cols + j;
        }
    }
}

int main(void) {
    int n = __VERIFIER_nondet_int();
    int values[n];
    for (int i = 0; i < n; ++i) {
        values[i] = i + 1;
    }
    printf("%u %d\n", (unsigned)sizeof(values), sum(n, values));

    int m = __VERIFIER_nondet_int();
    double grid[n][m];
    printf("%u %u %u\n", (unsigned)sizeof(grid), (unsigned)sizeof(grid[0]), (unsigned)sizeof grid[0][0]);

    int table[n][m];
    fill(n, m, table);
    printf("%d %d\n", table[n - 1][m - 1], table[1][0]);
    int (*rowPointer)[m] = table;
    printf("%d %d\n", rowPointer[2][1], (int)((char *)(rowPointer + 1) - (char *)rowPointer));

    typedef char Buffer[m * 2];
    m = 100; // the size of a VLA type is fixed when its declaration is reached
    Buffer buffer;
    printf("%u\n", (unsigned)sizeof(Buffer) + (unsigned)sizeof buffer);

    for (int k = 1; k <= 3; ++k) {
        char scratch[k * 4];
        scratch[k * 4 - 1] = (char)k;
        printf("%u:%d ", (unsigned)sizeof scratch, scratch[k * 4 - 1]);
    }
    printf("\n");
    printf("%u\n", (unsigned)sizeof(int[n][3]));
    return 0;
}
