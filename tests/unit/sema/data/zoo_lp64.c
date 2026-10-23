// Types and values of expressions on x86-64 (LP64), as GCC 14 sees them; see zoo_common.c.

unsigned u;
long l;
unsigned long ul;
long long ll;
int *p;
struct mixed { char c; double d; long long ll; long double ld; };
struct bits { unsigned a : 3; unsigned b : 32; unsigned long long c : 20; unsigned long long d : 32; int e : 31; };
enum wide { WIDE_A = -1, WIDE_B = 0x80000000 };
enum huge { HUGE_A = 0xffffffff, HUGE_B };

_Static_assert(__builtin_types_compatible_p(__typeof__(2147483648), long), "2147483648");
_Static_assert(__builtin_types_compatible_p(__typeof__(0x100000000), long), "0x100000000");
_Static_assert(__builtin_types_compatible_p(__typeof__(0xffffffffffffffff), unsigned long), "0xffffffffffffffff");
_Static_assert(__builtin_types_compatible_p(__typeof__(L'a'), int), "L'a'");
_Static_assert(__builtin_types_compatible_p(__typeof__(L"ab"), int[3]), "L\"ab\"");
_Static_assert(__builtin_types_compatible_p(__typeof__(l + u), long), "l + u");
_Static_assert(__builtin_types_compatible_p(__typeof__(ll + ul), unsigned long long), "ll + ul");
_Static_assert(__builtin_types_compatible_p(__typeof__(sizeof(int)), unsigned long), "size_t");
_Static_assert(__builtin_types_compatible_p(__typeof__(p - p), long), "ptrdiff_t");
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_offsetof(struct mixed, d)), unsigned long), "offsetof");
_Static_assert(__builtin_types_compatible_p(__typeof__(WIDE_B), enum wide), "WIDE_B");
_Static_assert(__builtin_types_compatible_p(enum wide, long), "enum wide");
_Static_assert(__builtin_types_compatible_p(enum huge, unsigned long), "enum huge");
_Static_assert(HUGE_B == 0x100000000, "HUGE_B");
_Static_assert(__builtin_types_compatible_p(__typeof__((__int128)1 + 1ul), __int128), "__int128 + unsigned long");
_Static_assert(__builtin_types_compatible_p(__typeof__(__atomic_fetch_add(&l, 1, 0)), long), "atomic long");

_Static_assert(sizeof(long) == 8 && sizeof(void *) == 8 && sizeof(long double) == 16, "sizes");
_Static_assert(_Alignof(long long) == 8 && _Alignof(double) == 8 && _Alignof(long double) == 16, "_Alignof");
_Static_assert(sizeof(struct mixed) == 48 && __builtin_offsetof(struct mixed, ld) == 32, "struct mixed");
_Static_assert(sizeof(struct bits) == 24, "struct bits");
_Static_assert(sizeof(__int128) == 16 && _Alignof(__int128) == 16, "__int128");
