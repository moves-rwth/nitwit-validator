// Member access (C17 6.5.2.3): `.` and `->`, qualifiers, rvalue bases and anonymous members.
struct node {
    int value;
    struct node *next;
    union {
        int i;
        struct {
            short lo, hi;
        };
    };
    unsigned flags : 3;
};

struct node n;
const struct node cn;
struct node *ptr;
struct node make(void);

void members(void) {
    n.value;
    ptr->next->value;
    cn.value;
    n.i;
    ptr->hi;
    make().value;
    n.flags + 1;
    n.flags = 5;
}
