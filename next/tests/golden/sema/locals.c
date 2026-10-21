// The locals of functions: parameters first, then automatic variables in declaration order; variable length arrays.
int global;

int locals(int n, char *argv[]) {
    int total = 0;
    static int calls;
    extern int global;
    for (int i = 0; i < n; i++) {
        int matrix[n][2];
        total += sizeof matrix;
    }
    {
        register int r = n;
        total += r;
    }
    typedef char buffer[n];
    buffer b;
    return total + sizeof b + calls + global;
}
