// Inline assembly: only templates are kept; asm labels and file-scope asm are skipped.
__asm__(".symver memcpy,memcpy@GLIBC_2.2.5");
int renamed(void) __asm__("other_name");

void barrier(int *p) {
    __asm__ __volatile__("" : : : "memory");
    asm volatile("mov %1, %0" : "=r"(*p) : "r"(*p + 1));
    asm goto("" : : : : done);
done:
    ;
}
