// Variadic functions (C17 7.16) with the builtins that <stdarg.h> expands to; default argument promotions.
int printf(const char *, ...);
int vprintf(const char *, __builtin_va_list);
int __VERIFIER_nondet_int(void);

typedef __builtin_va_list va_list;

int sumInts(int count, ...) {
    va_list args;
    __builtin_va_start(args, count);
    int total = 0;
    for (int i = 0; i < count; ++i) {
        total += __builtin_va_arg(args, int);
    }
    __builtin_va_end(args);
    return total;
}

// float arguments are promoted to double, char and short to int.
double average(int count, ...) {
    va_list args;
    __builtin_va_start(args, count);
    double total = 0;
    for (int i = 0; i < count; ++i) {
        total += __builtin_va_arg(args, double);
    }
    __builtin_va_end(args);
    return total / count;
}

// A small formatter: 'i' int, 'l' long long, 'd' double, 's' string, 'p' pointer compared to null.
void describe(const char *types, ...) {
    va_list args;
    __builtin_va_start(args, types);
    for (const char *t = types; *t; ++t) {
        switch (*t) {
        case 'i':
            printf("int %d; ", __builtin_va_arg(args, int));
            break;
        case 'l':
            printf("long long %lld; ", __builtin_va_arg(args, long long));
            break;
        case 'd':
            printf("double %g; ", __builtin_va_arg(args, double));
            break;
        case 's':
            printf("string %s; ", __builtin_va_arg(args, const char *));
            break;
        case 'p':
            printf("pointer %d; ", __builtin_va_arg(args, void *) != 0);
            break;
        }
    }
    __builtin_va_end(args);
    printf("\n");
}

// va_copy lets a second pass read the same arguments.
int maxTwice(int count, ...) {
    va_list args, again;
    __builtin_va_start(args, count);
    __builtin_va_copy(again, args);
    int best = __builtin_va_arg(args, int);
    for (int i = 1; i < count; ++i) {
        int value = __builtin_va_arg(args, int);
        best = value > best ? value : best;
    }
    int sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += __builtin_va_arg(again, int);
    }
    __builtin_va_end(again);
    __builtin_va_end(args);
    return best * 1000 + sum;
}

void logMessage(const char *format, ...) {
    va_list args;
    __builtin_va_start(args, format);
    printf("[log] ");
    vprintf(format, args);
    __builtin_va_end(args);
}

int main(void) {
    char c = 'A';
    short s = -2;
    float f = 1.5f;
    printf("%d %d\n", sumInts(3, 1, 2, 3), sumInts(3, c, s, 10));
    printf("%g\n", average(3, 1.0, f, 4.5));
    int x = 42;
    describe("ildsp", 7, 1LL << 40, 2.25, (const char *)"text", (void *)&x);
    int k = __VERIFIER_nondet_int();
    printf("%d\n", maxTwice(4, 3, k, -1, 8));
    logMessage("%s=%d, %c%c\n", "k", k, 'o', 'k');
    return 0;
}
