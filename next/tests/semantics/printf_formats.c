// printf conversions as glibc implements them: flags, width, precision, length modifiers, %n-free; also puts and
// putchar, and the return value of printf.
int printf(const char *, ...);
int puts(const char *);
int putchar(int);
int __VERIFIER_nondet_int(void);

int main(void) {
    printf("[%d] [%i] [%u] [%x] [%X] [%o] [%c] [%s] [%%]\n", -42, 42, 42u, 255, 255, 8, 'z', "str");
    printf("[%5d] [%-5d] [%05d] [%+d] [% d] [%+d]\n", 42, 42, 42, 42, 42, -42);
    printf("[%#x] [%#o] [%#X] [%#x]\n", 255, 8, 255, 0);
    printf("[%.3d] [%8.3d] [%-8.3d|] [%.0d]\n", 7, 7, 7, 0);
    printf("[%.2s] [%10s] [%-10s|] [%.*s]\n", "abcdef", "right", "left", 3, "precision");
    printf("[%*d] [%-*d|] [%*.*f]\n", 6, 42, 6, 42, 9, 2, 3.14159);

    printf("[%hhd] [%hhu] [%hd] [%hu]\n", 300, 300, 70000, 70000);
    printf("[%ld] [%lu] [%lld] [%llu] [%llx]\n", -1L, 1UL << 31, -1LL, 18446744073709551615ULL, 1ULL << 40);
    printf("[%zu] [%zx]\n", sizeof(long long), sizeof(char[300]));

    printf("[%f] [%.2f] [%10.3f] [%-10.1f|] [%+.1f] [%08.2f]\n", 3.14159, 3.14159, 3.14159, 3.14159, 3.14159, -3.14159);
    printf("[%e] [%.2E] [%g] [%G] [%g] [%g]\n", 12345.678, 12345.678, 12345.678, 0.000012345, 1e-5, 123456789.0);
    printf("[%a] [%A] [%.2a]\n", 1.0, 255.5, 1.0 / 3);
    printf("[%#g] [%#.0f] [%.0e]\n", 1.0, 2.0, 15.0);
    printf("[%5.1f%%]\n", 99.5);

    int length = printf("12345\n");
    printf("%d\n", length);
    puts("puts adds a newline");
    putchar('o');
    putchar('k');
    putchar('\n');

    int k = __VERIFIER_nondet_int();
    printf("[%d] [%x] [%o] [%08X] [%-6d|] [%c]\n", k, k, k, k, k, 'a' + k % 26);
    printf("[%.*f] [%*s]\n", k % 5, 2.0 / 3, k % 7, "w");
    return 0;
}
