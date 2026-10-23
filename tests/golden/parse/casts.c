// Casts, compound literals, sizeof and alignof.
struct pair { int a, b; };
long l;
int *ip;

void casts(void) {
    int i = (int)l;
    char *cp = (char *)(void *)ip;
    i = (int)(unsigned char)'x';
    struct pair p = (struct pair){ 1, 2 };
    int *arr = (int[]){ 1, 2, 3 };
    i = ((struct pair){ .b = 5 }).b;
    l = sizeof(struct pair) + sizeof (int[4]){ 0 } + sizeof l;
    l = _Alignof(double) + __alignof__(double) + __alignof__ l;
}

int *file_scope_literal = (int[]){ 4, 5 };
