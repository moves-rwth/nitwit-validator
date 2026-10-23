// Builtins: library signatures for implicitly declared functions, GCC builtins, and the type-generic ones.
void builtins(int n, ...) {
    char *buffer = malloc(n);
    memset(buffer, 0, n);
    if (__builtin_expect(buffer == 0, 0))
        abort();
    int overflow;
    __builtin_add_overflow(n, 1, &overflow);
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    long next = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    free(buffer);
}
