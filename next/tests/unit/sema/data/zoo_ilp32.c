// Types and values of expressions on i386 (ILP32), as GCC 14 sees them; see zoo_common.c.

unsigned u;
long l;
unsigned long ul;
long long ll;
int *p;
struct mixed { char c; double d; long long ll; long double ld; };
struct bits { unsigned a : 3; unsigned b : 32; unsigned long long c : 20; unsigned long long d : 32; int e : 31; };
enum wide { WIDE_A = -1, WIDE_B = 0x80000000 };
enum huge { HUGE_A = 0xffffffff, HUGE_B };

_Static_assert(__builtin_types_compatible_p(__typeof__(2147483648), long long), "2147483648");
_Static_assert(__builtin_types_compatible_p(__typeof__(0x100000000), long long), "0x100000000");
_Static_assert(__builtin_types_compatible_p(__typeof__(0xffffffffffffffff), unsigned long long), "0xffff...");
_Static_assert(__builtin_types_compatible_p(__typeof__(L'a'), long), "L'a'");
_Static_assert(__builtin_types_compatible_p(__typeof__(L"ab"), long[3]), "L\"ab\"");
_Static_assert(__builtin_types_compatible_p(__typeof__(l + u), unsigned long), "l + u");
_Static_assert(__builtin_types_compatible_p(__typeof__(ll + ul), long long), "ll + ul");
_Static_assert(__builtin_types_compatible_p(__typeof__(sizeof(int)), unsigned int), "size_t");
_Static_assert(__builtin_types_compatible_p(__typeof__(p - p), int), "ptrdiff_t");
_Static_assert(__builtin_types_compatible_p(__typeof__(WIDE_B), enum wide), "WIDE_B");
_Static_assert(__builtin_types_compatible_p(enum wide, long long), "enum wide");
_Static_assert(__builtin_types_compatible_p(enum huge, unsigned long long), "enum huge");
_Static_assert(HUGE_B == 0x100000000, "HUGE_B");
_Static_assert(__builtin_types_compatible_p(__typeof__(__atomic_fetch_add(&ll, 1, 0)), long long), "atomic");

_Static_assert(sizeof(long) == 4 && sizeof(void *) == 4 && sizeof(long double) == 12, "sizes");
_Static_assert(_Alignof(long long) == 4 && _Alignof(double) == 4 && _Alignof(long double) == 4, "_Alignof");
_Static_assert(__alignof__(long long) == 8 && __alignof__(double) == 8 && __alignof__(long double) == 4,
               "__alignof__");
_Static_assert(sizeof(struct mixed) == 32 && __builtin_offsetof(struct mixed, ld) == 20, "struct mixed");
_Static_assert(sizeof(struct bits) == 20, "struct bits");
