// Initializers in semantic form (C17 6.7.9): designators, brace elision, unions, ranges and arrays of unknown size.
struct point { int x, y; };
struct line { struct point from, to; };
union value { char c; double d; };

struct point origin = { 0 };
struct point p1 = { .y = 2, .x = 1 };
struct line l1 = { .from.y = 1, 2, 3 };
struct line l2 = { 1, 2, 3, 4 };
int squares[] = { [1] = 1, [2] = 4, [4 ... 5] = 9 };
int matrix[2][3] = { 1, 2, 3, { 4 } };
union value v1 = { 'a' };
union value v2 = { .d = 2 };
struct point old = { y: 5, x: 6 };
double scalar = { 7 };
int empty[3] = {};

void locals(void) {
    struct point p = { .x = 1 };
    long values[] = { 1, 2 };
    struct line *l = &(struct line){ .to = p };
}
