// Arrays: partial initialization, multidimensional arrays, sizeof, decay to pointers in expressions and parameters.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int sumArray(int values[], int count) {
    int total = 0;
    for (int i = 0; i < count; ++i) {
        total += values[i];
    }
    return total + (int)sizeof(values);
}

void scale(int grid[][3], int rows, int factor) {
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < 3; ++j) {
            grid[i][j] *= factor;
        }
    }
}

int global[5] = {1, 2};

int main(void) {
    int partial[6] = {7, 8, 9};
    printf("%d %d %d %d\n", partial[2], partial[3], partial[5], global[4]);
    printf("%u %u %u\n", (unsigned)sizeof(partial), (unsigned)(sizeof partial / sizeof partial[0]),
           (unsigned)sizeof(&partial));

    int implicit[] = {1, 2, 3, 4};
    printf("%u\n", (unsigned)sizeof(implicit));
    printf("%d\n", sumArray(implicit, 4));

    int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
    scale(grid, 2, 10);
    printf("%d %d %d\n", grid[0][0], grid[1][2], grid[1][0]);
    printf("%u %u\n", (unsigned)sizeof(grid), (unsigned)sizeof(grid[1]));

    int braces[2][2] = {1, 2, 3};
    printf("%d %d %d %d\n", braces[0][0], braces[0][1], braces[1][0], braces[1][1]);

    char name[10] = "abc";
    printf("%s %d %d\n", name, name[3], name[9]);
    const char *words[] = {"zero", "one", "two"};
    printf("%s %c %u\n", words[1], words[2][1], (unsigned)(sizeof words / sizeof *words));

    int k = __VERIFIER_nondet_int();
    partial[k] = 100;
    printf("%d %d\n", partial[k], *(partial + k));
    printf("%d\n", k[implicit]);

    int cube[2][2][2];
    int n = 0;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            for (int l = 0; l < 2; ++l) {
                cube[i][j][l] = n++;
            }
        }
    }
    printf("%d %d %u\n", cube[1][0][1], cube[k % 2][1][1], (unsigned)sizeof(cube[0]));
    return 0;
}
