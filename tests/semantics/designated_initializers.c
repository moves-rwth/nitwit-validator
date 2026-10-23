// Designated initializers (C17 6.7.9): array and member designators, nesting, continuation after a designator,
// overriding, brace elision and zero-filling.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct Inner {
    int a;
    int b;
};

struct Outer {
    int id;
    struct Inner inner;
    int list[3];
    const char *name;
};

union Value {
    int i;
    float f;
    char bytes[4];
};

int main(void) {
    int sparse[8] = {[3] = 7, [1] = 2, 9, [6] = 1};
    for (int i = 0; i < 8; ++i) {
        printf("%d ", sparse[i]);
    }
    printf("\n");
    int sized[] = {[4] = 1, 2};
    printf("%u %d\n", (unsigned)(sizeof sized / sizeof sized[0]), sized[5]);
    int overridden[3] = {[0] = 1, [0] = 2, [2] = 3};
    printf("%d %d %d\n", overridden[0], overridden[1], overridden[2]);

    struct Outer o = {.name = "outer", .inner.b = 5, .list = {[2] = 30}, .id = 1};
    printf("%d %d %d %d %d %d %s\n", o.id, o.inner.a, o.inner.b, o.list[0], o.list[1], o.list[2], o.name);
    struct Outer elided = {2, 3, 4, 5, 6};
    printf("%d %d %d %d %d %d\n", elided.id, elided.inner.a, elided.inner.b, elided.list[0], elided.list[1],
           elided.list[2]);
    struct Outer mixed = {.inner = {.b = 8}, 9, 10};
    printf("%d %d %d %d\n", mixed.inner.a, mixed.inner.b, mixed.list[0], mixed.list[1]);

    struct Inner points[4] = {[1].a = 1, [1].b = 2, [3] = {3, 4}};
    for (int i = 0; i < 4; ++i) {
        printf("(%d,%d)", points[i].a, points[i].b);
    }
    printf("\n");

    union Value v = {.f = 1.5f};
    union Value w = {.bytes = {1, 1}};
    printf("%x %d\n", (unsigned)v.i, w.i);
    char letters[6] = {[0] = 'a', [4] = 'e'};
    printf("%c %d %c %d\n", letters[0], letters[1], letters[4], letters[5]);

    int k = __VERIFIER_nondet_int();
    int runtime[4] = {[2] = k, [0] = k + 1};
    struct Inner fromInput = {.b = k * 2};
    printf("%d %d %d %d %d %d\n", runtime[0], runtime[1], runtime[2], runtime[3], fromInput.a, fromInput.b);
    return 0;
}
