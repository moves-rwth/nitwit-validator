// Storage classes, qualifiers and several declarators per declaration.
static int counter;
extern const char *name;
int a = 1, *b = &a, c[3];
volatile unsigned long long big;
int r(register int x);
_Thread_local int perThread;
static _Noreturn void stop(void);
inline int twice(int x) { return 2 * x; }
