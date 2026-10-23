// Structs as values: assignment, passing and returning by value (small and large structs), arrays inside structs.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct Small {
    int x;
    int y;
};

struct Large {
    int id;
    double values[8];
    char tag[12];
    struct Small corner;
};

struct Small makeSmall(int x, int y) {
    struct Small s = {x, y};
    return s;
}

struct Large makeLarge(int id) {
    struct Large large = {id};
    for (int i = 0; i < 8; ++i) {
        large.values[i] = id + i * 0.5;
    }
    large.tag[0] = 'L';
    large.tag[1] = (char)('0' + id % 10);
    large.tag[2] = '\0';
    large.corner = makeSmall(id, -id);
    return large;
}

// The callee works on a copy.
int consume(struct Large large) {
    large.values[0] = -1000;
    large.corner.x = 0;
    return (int)large.values[7] + large.corner.y;
}

struct Small swap(struct Small s) {
    int t = s.x;
    s.x = s.y;
    s.y = t;
    return s;
}

int main(void) {
    struct Small a = {1, 2};
    struct Small b = a;
    b.x = 10;
    printf("%d %d %d %d\n", a.x, a.y, b.x, b.y);

    struct Small c, d;
    c = d = makeSmall(3, 4);
    d.y = 40;
    printf("%d %d %d %d\n", c.x, c.y, d.x, d.y);
    printf("%d %d\n", swap(c).x, makeSmall(5, 6).y);

    int k = __VERIFIER_nondet_int();
    struct Large large = makeLarge(k);
    struct Large copy = large;
    int result = consume(large);
    copy.tag[0] = 'C';
    printf("%d %g %g %s %s %d %d\n", result, large.values[0], large.values[7], large.tag, copy.tag, large.corner.x,
           large.corner.y);
    printf("%g %s\n", makeLarge(k + 1).values[3], makeLarge(7).tag);

    struct Large array[2];
    array[0] = makeLarge(1);
    array[1] = array[0];
    array[1].corner = swap(array[1].corner);
    printf("%d %d %d %d\n", array[0].corner.x, array[0].corner.y, array[1].corner.x, array[1].corner.y);

    struct Small *pointer = &array[1].corner;
    struct Small viaPointer = *pointer;
    *pointer = (struct Small){99, 98};
    printf("%d %d %d\n", viaPointer.x, array[1].corner.x, array[1].corner.y);
    return 0;
}
