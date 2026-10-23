// sizeof, _Alignof, __alignof__ and __builtin_offsetof, whose operands are not converted or evaluated.
struct s { char c; double d; struct { short x[3]; } in; };
struct s v;
int a[7];

unsigned long sizes(int n) {
    int vla[n];
    return sizeof(int) + sizeof a + sizeof(a[0]) + sizeof(struct s) + _Alignof(double) + __alignof__(v.d)
           + __builtin_offsetof(struct s, in.x[2]) + sizeof vla + sizeof(n++);
}
