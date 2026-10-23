// Compound literals (C17 6.5.2.5): arrays and structs, as lvalues, as arguments, at file scope (static storage).
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct Point {
    int x;
    int y;
};

int *globalValues = (int[]){4, 5, 6};
struct Point *origin = &(struct Point){0, 0};

int length(struct Point p) {
    return p.x * p.x + p.y * p.y;
}

int sum(const int *values, int count) {
    int total = 0;
    for (int i = 0; i < count; ++i) {
        total += values[i];
    }
    return total;
}

int main(void) {
    int *p = (int[]){1, 2, 3};
    p[1] = 20;
    printf("%d %d %d\n", p[0], p[1], p[2]);
    printf("%d %d\n", length((struct Point){3, 4}), sum((int[]){10, 20, 30, 40}, 4));
    printf("%u\n", (unsigned)sizeof((int[]){1, 2, 3, 4, 5}));

    struct Point q = (struct Point){.y = 7};
    printf("%d %d\n", q.x, q.y);
    (struct Point){1, 2}.x; // an unused compound literal is fine
    int x = (struct Point){5, 6}.y;
    printf("%d\n", x);

    struct Point *pointer = &(struct Point){8, 9};
    pointer->x += 1;
    printf("%d %d\n", pointer->x, pointer->y);

    globalValues[2] += 10;
    origin->y = -1;
    printf("%d %d %d %d\n", globalValues[0], globalValues[2], origin->x, origin->y);

    int k = __VERIFIER_nondet_int();
    int *dynamic = (int[]){k, k * 2, k * 3};
    printf("%d %d\n", dynamic[2], (int[]){7, 8, 9}[k % 3]);
    const char *word = (const char[]){'h', 'i', '\0'};
    printf("%s %c\n", word, ((char[]){"abc"})[k % 3]);
    return 0;
}
