// Pointer arithmetic, subscripts and comparisons (C17 6.5.2.1, 6.5.6, 6.5.8, 6.5.9).
int a[10];
int m[3][4];
int *p, *q;
void *v;
char c;

void pointers(void) {
    p + 1;
    2 + p;
    p - c;
    p - q;
    v + 1;
    a[3];
    3[a];
    m[1][2];
    *(a + 1);
    &a;
    &a[2];
    &*p;
    p == q;
    p < q;
    p != 0;
    0 == p;
    p == v;
    !p;
    p++;
    --q;
}
