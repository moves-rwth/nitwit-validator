// Typedef names, and the typedef/identifier ambiguities.
typedef unsigned long size_t;
typedef int T, *PT, AT[2];
typedef int F(int);
F apply;
size_t n;
int x;

void ambiguities(void) {
    T * y;
    (T)(x);
    sizeof(T);
    {
        int T = 3;
        T * x;
    }
    unsigned T;
}

void parameters(int T) { T * x; }
