// Pre-C99 code: implicit int and implicit function declarations.
static counter;
const limit = 10;

main() {
    register i;
    for (i = 0; i < limit; i++)
        helper(i);
    return compute(counter) + helper(0);
}

int helper(int value) { return value; }
