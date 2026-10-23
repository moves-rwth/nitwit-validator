// Static storage duration: static locals keep their value between calls and are initialized once; file-scope
// statics; uninitialized statics are zero.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

static int fileCounter;
static const char *names[] = {"zero", "one", "two"};

int counter(void) {
    static int count = 10;
    return count++;
}

int otherCounter(void) {
    static int count; // a different object than the static in counter
    return ++count;
}

int *history(int value) {
    static int values[4];
    static int next = 0;
    values[next % 4] = value;
    ++next;
    return values;
}

int depth(int n) {
    static int deepest = 0;
    if (n > deepest) {
        deepest = n;
    }
    if (n < 3) {
        depth(n + 1);
    }
    return deepest;
}

const char *name(int i) {
    static char buffer[16];
    const char *source = names[i];
    int j = 0;
    for (; source[j]; ++j) {
        buffer[j] = source[j];
    }
    buffer[j] = '\0';
    return buffer;
}

int main(void) {
    // The calls are in separate statements: the order of evaluation of arguments is unspecified.
    int first = counter();
    int second = counter();
    int third = counter();
    printf("%d %d %d\n", first, second, third);
    first = otherCounter();
    second = otherCounter();
    printf("%d %d\n", first, second);
    history(5);
    history(6);
    int *h = history(7);
    printf("%d %d %d %d\n", h[0], h[1], h[2], h[3]);
    first = depth(0);
    second = depth(5);
    printf("%d %d\n", first, second);
    ++fileCounter;
    printf("%d %s\n", fileCounter, name(2));

    int k = __VERIFIER_nondet_int();
    for (int i = 0; i < k; ++i) {
        counter();
    }
    printf("%d\n", counter());
    for (int i = 0; i < 3; ++i) {
        static int once = 100; // initialized before the program starts, not on every iteration
        int every = 100;
        ++once;
        ++every;
        printf("%d %d\n", once, every);
    }
    return 0;
}
