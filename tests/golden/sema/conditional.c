// The conditional operator (C17 6.5.15) with arithmetic, pointer, void and structure operands, and GNU `?:`.
int i;
double d;
int *ip;
const int *cip;
void *vp;
struct s { int a; } s1, s2;
void nothing(void);

void conditional(int c) {
    c ? i : d;
    c ? ip : 0;
    c ? ip : cip;
    c ? ip : vp;
    c ? s1 : s2;
    c ? nothing() : nothing();
    i ?: d;
    ip ?: vp;
}
