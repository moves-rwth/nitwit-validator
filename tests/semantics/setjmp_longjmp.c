// setjmp and longjmp (C11 7.13): the values setjmp returns, jumps across frames and out of the scope of a variable
// length array, jmp_bufs in static, automatic and heap memory and copies of them, the uses of setjmp of C11 7.13.1.1p4
// and as the value of an assignment or initializer, glibc's names, and a value of longjmp from an input. Automatic
// objects that change between setjmp and longjmp are volatile, so that their values are determinate (C11 7.13.2.1p3).
int printf(const char *, ...);
void *malloc(unsigned long);
void *memcpy(void *, const void *, unsigned long);
int __VERIFIER_nondet_int(void);

typedef long __jmp_buf[8];
struct __jmp_buf_tag {
    __jmp_buf __jmpbuf;
    int __mask_was_saved;
    unsigned long __saved_mask[1024 / (8 * sizeof(unsigned long))];
};
typedef struct __jmp_buf_tag jmp_buf[1];
int setjmp(struct __jmp_buf_tag __env[1]);
int _setjmp(struct __jmp_buf_tag __env[1]);
int __sigsetjmp(struct __jmp_buf_tag __env[1], int __savemask);
void longjmp(struct __jmp_buf_tag __env[1], int __val) __attribute__((__noreturn__));
void _longjmp(struct __jmp_buf_tag __env[1], int __val) __attribute__((__noreturn__));
void siglongjmp(struct __jmp_buf_tag __env[1], int __val) __attribute__((__noreturn__));
void __longjmp_chk(struct __jmp_buf_tag __env[1], int __val) __attribute__((__noreturn__));

static jmp_buf env;

static void jump(int value) {
    longjmp(env, value);
}

static void down(int n, int value) {
    volatile int here = n;
    if (n == 0) {
        jump(value);
    }
    down(n - 1, value);
    printf("not reached %d\n", here);
}

// setjmp returns 0, then the value of each longjmp, or 1 for 0 (C11 7.13.2.1p4); the frames between end.
static void values(void) {
    volatile int round = 0;
    int value = setjmp(env);
    printf("setjmp returned %d after round %d\n", value, round);
    if (round++ < 3) {
        down(round * 2, round == 2 ? 0 : round * 10);
    }
}

static void contexts(void) {
    volatile int step = 0;
    if (!setjmp(env)) {
        jump(1);
    }
    printf("!setjmp\n");
    if (setjmp(env) == 0) {
        jump(2);
    } else {
        printf("setjmp == 0 is false\n");
    }
    if (3 != _setjmp(env)) {
        jump(3);
    }
    printf("3 != _setjmp\n");
    switch (setjmp(env)) {
    case 0:
        jump(4);
    case 4:
        printf("switch case 4\n");
        break;
    default:
        printf("switch default\n");
        break;
    }
    while (setjmp(env) < 5) {
        jump(5);
    }
    printf("while\n");
    do {
        if (step == 1) {
            jump(6);
        }
        step++;
    } while (!setjmp(env));
    printf("do while, step %d\n", step);
    (void)_setjmp(env);
    if (++step == 2) {
        jump(7);
    }
    printf("(void)_setjmp, step %d\n", step);
    int x = setjmp(env);
    if (x == 0) {
        jump(8);
    }
    printf("initializer %d\n", x);
    static int s;
    s = setjmp(env);
    if (s == 0) {
        jump(9);
    }
    printf("assignment %d\n", s);
}

static void buffers(void) {
    struct holder {
        jmp_buf env;
    } a, b;
    struct __jmp_buf_tag *heap = malloc(sizeof(jmp_buf));
    volatile int round = 0;
    int value = setjmp(a.env);
    printf("buffers %d\n", value);
    if (round == 0) {
        round = 1;
        b = a;
        longjmp(b.env, 11);
    }
    if (round == 1) {
        round = 2;
        memcpy(heap, a.env, sizeof(jmp_buf));
        longjmp(heap, 12);
    }
    if (setjmp(heap) == 0) {
        longjmp(heap, 13);
    }
    printf("heap\n");
}

static void names(void) {
    volatile int round = 0;
    int value = __sigsetjmp(env, 1);
    printf("names %d\n", value);
    if (round == 0) {
        round = 1;
        _longjmp(env, 21);
    }
    if (round == 1) {
        round = 2;
        siglongjmp(env, 22);
    }
    if (round == 2) {
        round = 3;
        __longjmp_chk(env, 23);
    }
}

// The jump leaves the scope of an array declared after the setjmp call.
static void arrays(int n) {
    volatile int round = 0;
    int value = setjmp(env);
    printf("arrays %d\n", value);
    if (round++ == 0) {
        int a[n];
        a[n - 1] = n;
        jump(a[n - 1]);
    }
}

static void input(void) {
    volatile int round = 0;
    int value = setjmp(env);
    printf("input %d\n", value);
    if (round++ < 2) {
        longjmp(env, __VERIFIER_nondet_int());
    }
}

int main(void) {
    values();
    contexts();
    buffers();
    names();
    arrays(31);
    input();
    printf("done\n");
    return 0;
}
