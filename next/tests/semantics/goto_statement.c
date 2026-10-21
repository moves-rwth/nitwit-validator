// goto (C17 6.8.6.1): backward and forward jumps, jumps into nested blocks and loop bodies, out of nested loops.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

int main(void) {
    int i = 0, total = 0;
again:
    total += i;
    if (++i < 5) {
        goto again;
    }
    printf("%d\n", total);

    int k = __VERIFIER_nondet_int();
    if (k == 2) {
        goto inner;
    }
    printf("skipped when k == 2\n");
    {
        int x; // jumping into the block skips nothing that needs initialization
        printf("block start\n");
    inner:
        x = 7 * k;
        printf("x = %d\n", x);
    }

    for (int a = 0; a < 4; ++a) {
        for (int b = 0; b < 4; ++b) {
            if (a * b == 6) {
                printf("found %d %d\n", a, b);
                goto done;
            }
        }
    }
    printf("not found\n");
done:;

    // Jump into the middle of a loop body: the first iteration starts at the label.
    int n = 0;
    goto middle;
    while (n < 3) {
        printf("top %d\n", n);
    middle:
        printf("middle %d\n", n);
        ++n;
    }

    // goto out of a block and back to a label before it re-enters the block.
    int rounds = 0;
retry:
    {
        int attempt = rounds * 10;
        ++rounds;
        if (rounds < 3) {
            printf("attempt %d\n", attempt);
            goto retry;
        }
    }
    printf("rounds %d\n", rounds);

    // A label in a switch body, reached by goto from outside the switch.
    int m = __VERIFIER_nondet_int();
    if (m > 0) {
        goto inSwitch;
    }
    switch (m) {
    case 0:
        printf("case 0\n");
    inSwitch:
        printf("in switch %d\n", m);
        break;
    default:
        printf("default\n");
    }
    return 0;
}
