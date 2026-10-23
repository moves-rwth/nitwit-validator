// Conversions as if by assignment (C17 6.5.16.1), including GCC's integer/pointer and pointer warnings.
int *ip;
const int *cip;
void *vp;
char *cp;
long l;
double d;
_Bool b;
struct pair { int a, b; } p, q;
int (*fp)(int);
int f(int);

void conversions(void) {
    l = d;
    d = l;
    b = d;
    b = ip;
    ip = 0;
    ip = (void *)0;
    vp = ip;
    ip = vp;
    cip = ip;
    ip = cip;
    cp = ip;
    ip = l;
    l = ip;
    p = q;
    fp = f;
    fp = &f;
}
