// Prototypes and definitions, variadic functions, K&R definitions, redeclarations.
int printf(const char *format, ...);
int max(int, int);
int max(int a, int b) { return a > b ? a : b; }
static int unprototyped();
static int unprototyped() { return max(1, 2); }

int old_style(a, b, c)
    char *b;
    double a;
{
    return c;
}

void (*callback)(void);
void call(void) {
    callback();
    (*callback)();
}
