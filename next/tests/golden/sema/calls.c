// Calls (C17 6.5.2.2): arguments of prototypes, of functions without prototype, and after an ellipsis.
long with_prototype(long, double);
int without_prototype();
int variadic(const char *, ...);
void (*callback)(int);
struct big { int data[4]; };
struct big make(struct big b);

void calls(char c, float f, short s, struct big b) {
    with_prototype(c, f);
    without_prototype(c, f, s);
    variadic("%d %f", s, f);
    callback(c);
    (*callback)(1);
    make(b);
}
