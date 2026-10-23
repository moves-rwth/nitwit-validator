// Types and values of expressions on both data models, as GCC 14 sees them: each line is a static assertion that sema
// checks when it analyzes this file (zoo_test.cpp) and that gcc-14 -fsyntax-only checks with -m32 and -m64
// (tests/unit/sema/CMakeLists.txt). zoo_ilp32.c and zoo_lp64.c hold what differs between the models.

char c;
signed char sc;
unsigned char uc;
short s;
unsigned short us;
int i;
unsigned u;
long l;
unsigned long ul;
long long ll;
unsigned long long ull;
_Bool b;
float f;
double d;
long double ld;
int *p;
const int *cp;
volatile int *vp;
void *vptr;
int arr[3];
int matrix[2][3];
enum small { SMALL_A, SMALL_B } en;
enum negative { NEGATIVE_A = -1, NEGATIVE_B } neg;
enum { WIDE_A = 0x7fffffff, WIDE_B };
struct point { int x, y; } pt;
struct bits { unsigned a : 3; unsigned b : 32; unsigned long long c : 20; unsigned long long d : 32; int e : 31; } bf;
struct padded { char c; int i; short s; double d; } pad;
union mixed { char c[5]; int i; } mix;
int function(int);

// Literals (C17 6.4.4, 6.4.5).
_Static_assert(__builtin_types_compatible_p(__typeof__(1), int), "1");
_Static_assert(__builtin_types_compatible_p(__typeof__(2147483647), int), "2147483647");
_Static_assert(__builtin_types_compatible_p(__typeof__(0x7fffffff), int), "0x7fffffff");
_Static_assert(__builtin_types_compatible_p(__typeof__(0x80000000), unsigned int), "0x80000000");
_Static_assert(__builtin_types_compatible_p(__typeof__(0xffffffff), unsigned int), "0xffffffff");
_Static_assert(__builtin_types_compatible_p(__typeof__(1u), unsigned int), "1u");
_Static_assert(__builtin_types_compatible_p(__typeof__(1l), long), "1l");
_Static_assert(__builtin_types_compatible_p(__typeof__(1ul), unsigned long), "1ul");
_Static_assert(__builtin_types_compatible_p(__typeof__(1ll), long long), "1ll");
_Static_assert(__builtin_types_compatible_p(__typeof__(1ull), unsigned long long), "1ull");
_Static_assert(__builtin_types_compatible_p(__typeof__(0xffffffffffffffff), unsigned long long)
                   || __builtin_types_compatible_p(__typeof__(0xffffffffffffffff), unsigned long),
               "0xffffffffffffffff");
_Static_assert(__builtin_types_compatible_p(__typeof__('a'), int), "'a'");
_Static_assert(__builtin_types_compatible_p(__typeof__(u'a'), unsigned short), "u'a'");
_Static_assert(__builtin_types_compatible_p(__typeof__(U'a'), unsigned int), "U'a'");
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0f), float), "1.0f");
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0), double), "1.0");
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0L), long double), "1.0L");
_Static_assert(__builtin_types_compatible_p(__typeof__("ab"), char[3]), "\"ab\"");
_Static_assert(__builtin_types_compatible_p(__typeof__(u"ab"), unsigned short[3]), "u\"ab\"");
_Static_assert(__builtin_types_compatible_p(__typeof__(U"ab"), unsigned int[3]), "U\"ab\"");
_Static_assert(__builtin_types_compatible_p(__typeof__(u8"ab"), char[3]), "u8\"ab\"");
_Static_assert('\377' == -1, "'\\377'");
_Static_assert(u'\xffff' == 65535, "u'\\xffff'");
_Static_assert(L'\xffffffff' == -1, "L'\\xffffffff'");
_Static_assert(sizeof("abc") == 4, "sizeof \"abc\"");
_Static_assert(sizeof(L"ab") == 12, "sizeof L\"ab\"");
_Static_assert(sizeof(u"ab") == 6, "sizeof u\"ab\"");

// Integer promotions (C17 6.3.1.1), also of enumerations and bit-fields by their width.
_Static_assert(__builtin_types_compatible_p(__typeof__(c + c), int), "c + c");
_Static_assert(__builtin_types_compatible_p(__typeof__(uc + uc), int), "uc + uc");
_Static_assert(__builtin_types_compatible_p(__typeof__(us + us), int), "us + us");
_Static_assert(__builtin_types_compatible_p(__typeof__(b + b), int), "b + b");
_Static_assert(__builtin_types_compatible_p(__typeof__(-c), int), "-c");
_Static_assert(__builtin_types_compatible_p(__typeof__(~us), int), "~us");
_Static_assert(__builtin_types_compatible_p(__typeof__(+f), float), "+f");
_Static_assert(__builtin_types_compatible_p(__typeof__(!d), int), "!d");
_Static_assert(__builtin_types_compatible_p(__typeof__(en), unsigned int), "en");
_Static_assert(__builtin_types_compatible_p(__typeof__(en + 0), unsigned int), "en + 0");
_Static_assert(__builtin_types_compatible_p(__typeof__(neg + 0), int), "neg + 0");
_Static_assert(__builtin_types_compatible_p(__typeof__(SMALL_A), int), "SMALL_A");
_Static_assert(__builtin_types_compatible_p(__typeof__(WIDE_B), unsigned int), "WIDE_B");
_Static_assert(WIDE_B == 0x80000000, "WIDE_B value");
_Static_assert(__builtin_types_compatible_p(__typeof__(bf.a + 0u), unsigned int), "bf.a + 0u");
_Static_assert(__builtin_types_compatible_p(__typeof__(bf.a - 1), int), "bf.a - 1");
_Static_assert(__builtin_types_compatible_p(__typeof__(bf.b - 1), unsigned int), "bf.b - 1");
_Static_assert(__builtin_types_compatible_p(__typeof__(bf.c + 0), int), "bf.c + 0");
_Static_assert(__builtin_types_compatible_p(__typeof__(bf.d + 0), unsigned int), "bf.d + 0");
_Static_assert(__builtin_types_compatible_p(__typeof__(bf.e + 0), int), "bf.e + 0");
_Static_assert(__builtin_types_compatible_p(__typeof__((bf.a = 1) - 2), int), "(bf.a = 1) - 2");

// The usual arithmetic conversions (C17 6.3.1.8) where they do not depend on the data model.
_Static_assert(__builtin_types_compatible_p(__typeof__(i + u), unsigned int), "i + u");
_Static_assert(__builtin_types_compatible_p(__typeof__(i + ll), long long), "i + ll");
_Static_assert(__builtin_types_compatible_p(__typeof__(u + ll), long long), "u + ll");
_Static_assert(__builtin_types_compatible_p(__typeof__(ll + ull), unsigned long long), "ll + ull");
_Static_assert(__builtin_types_compatible_p(__typeof__(l + ul), unsigned long), "l + ul");
_Static_assert(__builtin_types_compatible_p(__typeof__(f + i), float), "f + i");
_Static_assert(__builtin_types_compatible_p(__typeof__(f + d), double), "f + d");
_Static_assert(__builtin_types_compatible_p(__typeof__(d + ld), long double), "d + ld");
_Static_assert(__builtin_types_compatible_p(__typeof__(f + ull), float), "f + ull");
_Static_assert(__builtin_types_compatible_p(__typeof__(c << ll), int), "c << ll");
_Static_assert(__builtin_types_compatible_p(__typeof__(1u << 1ll), unsigned int), "1u << 1ll");
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0 < 2), int), "1.0 < 2");
_Static_assert(__builtin_types_compatible_p(__typeof__(p == 0), int), "p == 0");
_Static_assert(__builtin_types_compatible_p(__typeof__(d && p), int), "d && p");

// Assignments, increments, the comma and conditional operators.
_Static_assert(__builtin_types_compatible_p(__typeof__(c = 1000), char), "c = 1000");
_Static_assert(__builtin_types_compatible_p(__typeof__(i += 1.5), int), "i += 1.5");
_Static_assert(__builtin_types_compatible_p(__typeof__(us++), unsigned short), "us++");
_Static_assert(__builtin_types_compatible_p(__typeof__(--f), float), "--f");
_Static_assert(__builtin_types_compatible_p(__typeof__((i, c)), char), "(i, c)");
_Static_assert(__builtin_types_compatible_p(__typeof__(i ? 1 : 2u), unsigned int), "i ? 1 : 2u");
_Static_assert(__builtin_types_compatible_p(__typeof__(i ? c : s), int), "i ? c : s");
_Static_assert(__builtin_types_compatible_p(__typeof__(i ? p : 0), int *), "i ? p : 0");
_Static_assert(__builtin_types_compatible_p(__typeof__(i ? p : vptr), void *), "i ? p : vptr");
_Static_assert(__builtin_types_compatible_p(__typeof__(i ? cp : vp), const volatile int *), "i ? cp : vp");
_Static_assert(__builtin_types_compatible_p(__typeof__(i ? pt : pt), struct point), "i ? pt : pt");
_Static_assert(__builtin_types_compatible_p(__typeof__(i ?: 2.0), double), "i ?: 2.0");

// Arrays, functions, pointers and members.
_Static_assert(__builtin_types_compatible_p(__typeof__(arr), int[3]), "arr");
_Static_assert(__builtin_types_compatible_p(__typeof__(arr + 0), int *), "arr + 0");
_Static_assert(__builtin_types_compatible_p(__typeof__(&arr), int(*)[3]), "&arr");
_Static_assert(__builtin_types_compatible_p(__typeof__(*&arr), int[3]), "*&arr");
_Static_assert(__builtin_types_compatible_p(__typeof__(matrix[1]), int[3]), "matrix[1]");
_Static_assert(__builtin_types_compatible_p(__typeof__(1 [arr]), int), "1[arr]");
_Static_assert(__builtin_types_compatible_p(__typeof__(function), int(int)), "function");
_Static_assert(__builtin_types_compatible_p(__typeof__(&function), int (*)(int)), "&function");
_Static_assert(__builtin_types_compatible_p(__typeof__(*function), int(int)), "*function");
_Static_assert(__builtin_types_compatible_p(__typeof__(p + 1), int *), "p + 1");
_Static_assert(__builtin_types_compatible_p(__typeof__(*cp), const int), "*cp");
_Static_assert(__builtin_types_compatible_p(__typeof__(pt.y), int), "pt.y");
_Static_assert(__builtin_types_compatible_p(__typeof__(mix.c), char[5]), "mix.c");
_Static_assert(__builtin_types_compatible_p(__typeof__((char)i), char), "(char)i");
_Static_assert(__builtin_types_compatible_p(__typeof__((const int)i), int), "(const int)i");
_Static_assert(__builtin_types_compatible_p(__typeof__((struct point){1, 2}), struct point), "compound literal");
_Static_assert(__builtin_types_compatible_p(__typeof__((int[]){1, 2, 3}), int[3]), "(int[]){1, 2, 3}");

// GNU expressions; statement expressions only in functions.
void statementExpressions(void) {
    _Static_assert(__builtin_types_compatible_p(__typeof__(({ 1; })), int), "({ 1; })");
    _Static_assert(__builtin_types_compatible_p(__typeof__(({ c; })), char), "({ c; })");
    _Static_assert(__builtin_types_compatible_p(__typeof__(({ arr; })), int *), "({ arr; })");
    _Static_assert(__builtin_types_compatible_p(__typeof__(({ goto done; done: l; })), long), "({ done: l; })");
    _Static_assert(__builtin_types_compatible_p(__typeof__(({ bf.a; }) - 1), int), "({ bf.a; }) - 1");
    _Static_assert(__builtin_types_compatible_p(__typeof__(({ bf.b; }) - 1), unsigned int), "({ bf.b; }) - 1");
}
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_choose_expr(1, 1, 2.0)), int), "choose");
_Static_assert(__builtin_types_compatible_p(__typeof__(_Generic(1L, long: 'a', default: 1.0)), int), "_Generic");
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_expect(i, 1)), long), "__builtin_expect");
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_isnan(f)), int), "__builtin_isnan");
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_fpclassify(0, 1, 2, 3, 4, f)), int), "fpclassify");
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_add_overflow(i, l, &c)), _Bool), "add_overflow");
_Static_assert(__builtin_types_compatible_p(__typeof__(__atomic_load_n(&i, 0)), int), "__atomic_load_n");
_Static_assert(__builtin_types_compatible_p(__typeof__(__atomic_fetch_add(&p, 1, 0)), int *), "__atomic_fetch_add");
_Static_assert(__builtin_types_compatible_p(__typeof__(__atomic_compare_exchange_n(&i, &i, 1, 0, 0, 0)), _Bool),
               "__atomic_compare_exchange_n");
_Static_assert(__builtin_types_compatible_p(__typeof__(__atomic_load(&pt, &pt, 0)), void), "__atomic_load");
_Static_assert(__builtin_types_compatible_p(__typeof__(__sync_fetch_and_add(&uc, 1)), unsigned char),
               "__sync_fetch_and_add");
_Static_assert(__builtin_types_compatible_p(__typeof__(__sync_bool_compare_and_swap(&l, 1, 2)), _Bool),
               "__sync_bool_compare_and_swap");

// Values of integer constant expressions, folded like GCC.
_Static_assert(7 / -2 == -3, "7 / -2");
_Static_assert(-7 % 2 == -1, "-7 % 2");
_Static_assert(-1 >> 1 == -1, "-1 >> 1");
_Static_assert((1 << 31) < 0, "1 << 31");
_Static_assert((unsigned char)-1 == 255, "(unsigned char)-1");
_Static_assert((signed char)200 == -56, "(signed char)200");
_Static_assert((unsigned short)70000 == 4464, "(unsigned short)70000");
_Static_assert((unsigned)-1 == 4294967295u, "(unsigned)-1");
_Static_assert(-1 < 0u == 0, "-1 < 0u");
_Static_assert((_Bool)256 == 1, "(_Bool)256");
_Static_assert((int)2.9 == 2, "(int)2.9");
_Static_assert((int)-2.9 == -2, "(int)-2.9");
_Static_assert(0x7fffffff + 0u == 2147483647u, "0x7fffffff + 0u");
_Static_assert(~0ull >> 63 == 1, "~0ull >> 63");
_Static_assert((1 ? 2 : (1 / 0)) == 2, "unevaluated division by zero");
_Static_assert((0 && (1 / 0)) == 0, "unevaluated &&");
_Static_assert(__builtin_constant_p(1.5) == 1, "__builtin_constant_p(1.5)");
_Static_assert(__builtin_constant_p("abc") == 1, "__builtin_constant_p(\"abc\")");
_Static_assert(__builtin_constant_p(i) == 0, "__builtin_constant_p(i)");
_Static_assert(__builtin_popcount(0xff00ff) == 16, "__builtin_popcount");
_Static_assert(__builtin_clzll(1) == 63, "__builtin_clzll");
_Static_assert(__builtin_bswap32(0x12345678) == 0x78563412, "__builtin_bswap32");
_Static_assert(__builtin_types_compatible_p(const int, int) == 1, "types_compatible_p qualifiers");
_Static_assert(__builtin_types_compatible_p(int[], int[3]) == 1, "types_compatible_p arrays");
_Static_assert(__builtin_types_compatible_p(enum small, unsigned int) == 1, "types_compatible_p enum");
_Static_assert(_Generic(1L, int: 1, long: 2, default: 3) == 2, "_Generic value");
_Static_assert(__builtin_choose_expr(0, 1, 2) == 2, "__builtin_choose_expr value");

// Sizes, alignments and offsets that do not depend on the data model.
_Static_assert(sizeof(char) == 1 && sizeof(short) == 2 && sizeof(int) == 4 && sizeof(long long) == 8, "sizes");
_Static_assert(sizeof(float) == 4 && sizeof(double) == 8, "floating sizes");
_Static_assert(sizeof(_Bool) == 1 && sizeof(en) == 4, "_Bool and enum sizes");
_Static_assert(__alignof__(double) == 8 && __alignof__(long long) == 8, "__alignof__");
_Static_assert(_Alignof(int) == 4 && _Alignof(short) == 2, "_Alignof");
_Static_assert(sizeof(struct point) == 8, "struct point");
_Static_assert(sizeof(union mixed) == 8 && _Alignof(union mixed) == 4, "union mixed");
_Static_assert(__builtin_offsetof(struct padded, i) == 4, "offsetof i");
_Static_assert(__builtin_offsetof(struct padded, s) == 8, "offsetof s");
_Static_assert(__builtin_offsetof(struct padded, d) == 12 || __builtin_offsetof(struct padded, d) == 16,
               "offsetof d");
_Static_assert(sizeof(arr) == 12 && sizeof(matrix[0]) == 12 && sizeof matrix == 24, "array sizes");
_Static_assert(sizeof(function(1)) == 4, "the operand of sizeof is not evaluated");
