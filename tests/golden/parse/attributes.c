// GNU attributes wherever GCC accepts them.
extern void __assert_fail(const char *, const char *, unsigned int, const char *)
    __attribute__((__nothrow__, __leaf__)) __attribute__((__noreturn__));
typedef int int8 __attribute__((__mode__(__QI__)));
struct __attribute__((packed)) packed { char c; int i; };
struct aligned { char c; } __attribute__((aligned(16)));
static int used __attribute__((used, section(".data")));
__attribute__((unused)) static int unused_variable;
int (__attribute__((cdecl)) *function_pointer)(int x __attribute__((unused)));
enum flags { F1 __attribute__((deprecated)) = 1 };
_Alignas(8) char buffer[3];

int labels(int a) {
    __attribute__((unused)) int local = 0;
again:
    __attribute__((unused));
    if (a--)
        goto again;
    return a;
}
