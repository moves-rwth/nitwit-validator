// Structs and unions: nested and anonymous members, bit-fields, flexible and zero-length arrays.
struct list;
struct list {
    int value;
    struct list *next;
};

struct outer {
    struct inner { char c; } in;
    union {
        int i;
        float f;
    };
    unsigned flag : 1, : 0, more : 3;
    int data[];
};

union u { int i; struct list *l; } global;
struct empty {};
struct zero { int n; char bytes[0]; };
typedef struct { int x, y; } point;

void use(void) {
    struct list { int shadow; } local;
    struct list *p = &local;
    point q;
}
