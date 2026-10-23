// Explicit casts (C17 6.5.4) and their kinds, including GNU casts to union types.
union number { int i; double d; };
int i;
double d;
int *p;
long l;

void casts(void) {
    (double)i;
    (int)d;
    (char)i;
    (_Bool)d;
    (_Bool)p;
    (long)p;
    (int *)l;
    (char *)p;
    (const int *)p;
    (int *)0;
    (void)i;
    (union number)d;
    (int)(char)300;
}
