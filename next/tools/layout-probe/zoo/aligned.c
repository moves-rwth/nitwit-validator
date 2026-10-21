// The `aligned` attribute and _Alignas on members, records and typedefs, including typedefs that lower the alignment.

struct a1 { char c; int i __attribute__((aligned(8))); };
struct a2 { char c; int i __attribute__((aligned(2))); };
struct a3 { char c; double d __attribute__((aligned(4))); };
struct a4 { char c; double d __attribute__((aligned(8))); };
struct a5 { char c; long long ll __attribute__((aligned(4))); };
struct a6 { char c; long long ll __attribute__((aligned(8))); };
struct a7 { char c; int i __attribute__((aligned)); };
struct __attribute__((aligned(16))) a8 { char c; };
struct __attribute__((aligned(2))) a9 { int i; };
struct a10 { char c; _Alignas(16) char d; };
struct a11 { char c; _Alignas(double) char d; };
struct a12 { char c; struct a8 inner; };
typedef long long aligned_ll __attribute__((aligned(8)));
struct a13 { char c; aligned_ll ll; };
typedef int int2 __attribute__((aligned(2)));
struct a14 { char c; int2 i; };
typedef double double4 __attribute__((aligned(4)));
struct a15 { char c; double4 d; };
typedef aligned_ll aligned_ll_again;
struct a16 { char c; aligned_ll_again ll[2]; };
struct a17 { char c; int i : 3 __attribute__((aligned(4))); char d; };
typedef struct a1 a1_16 __attribute__((aligned(16)));
struct a18 { char c; a1_16 s; };
struct a19 { long long a; char c; } __attribute__((aligned(4)));
union __attribute__((aligned(8))) a20 { char c; short s; };
struct a21 { char c; struct { char d; long long x __attribute__((aligned(8))); } inner; };

_Alignas(32) char object_aligned32;
int object_aligned16 __attribute__((aligned(16)));
aligned_ll object_aligned_ll;
