// Initializers: braces, designators, GNU range and obsolete designators, strings.
struct point { int x, y; };
struct line { struct point from, to; };

struct point origin = { 0, 0 };
struct point p1 = { .y = 2, .x = 1 };
struct line l1 = { .from.x = 1, .to = { 3, 4 } };
struct line l2 = { { 1, 2 }, 3, 4 };
int squares[5] = { [1] = 1, [2] = 4, [3 ... 4] = 9 };
int matrix[2][2] = { [0][1] = 1, { 2, 3 } };
struct point old = { y: 5, x: 6 };
char text[] = "hello";
char wide_text[] = { "hi" };
int empty[3] = {};
int scalar = { 7 };
