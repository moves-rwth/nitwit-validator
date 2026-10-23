// Loops (C17 6.8.5): for, while and do-while, break and continue, loop-scoped declarations, unsigned countdowns.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    int sum = 0;
    for (int i = 0; i < 10; ++i) {
        if (i % 3 == 0) {
            continue;
        }
        if (i == 8) {
            break;
        }
        sum += i;
    }
    printf("%d\n", sum);

    int i = 100; // shadowed by the loop variables below
    for (int i = 0; i < 3; ++i) {
        for (int i = 10; i < 12; ++i) {
            printf("%d ", i);
        }
    }
    printf("%d\n", i);

    int n = 0;
    while (n < 5) {
        n += 2;
    }
    printf("%d\n", n);

    // continue in a do-while jumps to the condition.
    int count = 0;
    do {
        ++count;
        if (count < 3) {
            continue;
        }
        printf("count %d\n", count);
    } while (count < 5);

    for (unsigned u = 3; u-- > 0;) {
        printf("u %u\n", u);
    }

    int limit = __VERIFIER_nondet_int();
    int steps = 0;
    for (;;) {
        if (steps >= limit) {
            break;
        }
        ++steps;
    }
    printf("steps %d\n", steps);

    int collatz = __VERIFIER_nondet_int();
    int length = 0;
    while (collatz != 1) {
        collatz = collatz % 2 == 0 ? collatz / 2 : 3 * collatz + 1;
        ++length;
    }
    printf("collatz length %d\n", length);

    int empty = 0;
    while (empty++ < 4)
        ;
    printf("%d\n", empty);
    for (int a = 0, b = 10; a < b; a += 3, b -= 2) {
        printf("(%d,%d)", a, b);
    }
    printf("\n");
    return 0;
}
