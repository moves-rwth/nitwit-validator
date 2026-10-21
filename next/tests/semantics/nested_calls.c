// Function calls nested in arguments, conditions and initializers. The functions are free of side effects, because
// the order in which arguments are evaluated is unspecified (C17 6.5.2.2p10).
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct Pair {
    int first;
    int second;
};

int square(int x) {
    return x * x;
}

int add(int a, int b) {
    return a + b;
}

int max(int a, int b) {
    return a > b ? a : b;
}

struct Pair makePair(int a, int b) {
    struct Pair pair = {a, b};
    return pair;
}

int sumPair(struct Pair pair) {
    return pair.first + pair.second;
}

double half(double x) {
    return x / 2;
}

// A call with argument and return conversions: char argument, long long result.
long long widen(char c, short s, float f) {
    return c * 1000LL + s + (long long)f;
}

int main(void) {
    printf("%d\n", add(square(3), square(4)));
    printf("%d\n", square(add(square(2), max(1, square(3)))));
    printf("%d\n", sumPair(makePair(square(2), add(1, 2))));
    printf("%d\n", max(max(1, 5), max(max(9, 2), 4)));
    printf("%g\n", half(half(square(5))));
    printf("%lld\n", widen(65, -3, 2.9f));
    printf("%d\n", makePair(add(1, 1), square(3)).second);

    int k = __VERIFIER_nondet_int();
    if (square(k) > add(k, 10)) {
        printf("square wins\n");
    }
    int total = 0;
    for (int i = 0; i < max(k, 3); i = add(i, 1)) {
        total = add(total, square(i));
    }
    printf("%d\n", total);
    int values[] = {square(k), add(k, k), max(k, -k)};
    printf("%d %d %d\n", values[0], values[1], values[2]);
    printf("%d\n", add(add(add(add(add(1, 2), 3), 4), 5), k));
    return 0;
}
