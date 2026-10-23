// The `packed` attribute on records and members, alone and with `aligned`, with bit-fields and nested records.

struct __attribute__((packed)) p1 { char c; int i; };
struct p2 { char c; int i; } __attribute__((packed));
struct p3 { char c; int i __attribute__((packed)); char d; long long l; };
struct __attribute__((packed)) p4 { char c; double d; short s; };
struct __attribute__((packed, aligned(4))) p5 { char c; int i; };
struct __attribute__((packed)) p6 { char c; int i __attribute__((aligned(4))); };
struct __attribute__((packed)) p7 { char c; struct { int x; char y; } inner; };
struct p8 { char c; struct p1 inner; };
struct __attribute__((packed)) p9 { char c : 3; int i : 20; char d; };
struct __attribute__((packed)) p10 { char c; long long ll : 40; };
struct p11 { char c; int i : 20 __attribute__((packed)); };
struct __attribute__((packed)) p12 { int a : 3; int b : 31; };
struct __attribute__((packed)) p13 { char c; char d[3]; long double ld; };
union __attribute__((packed)) p14 { char c; int i; double d; };
struct p15 { char c; union p14 u; };
struct __attribute__((packed)) p16 { char a; int : 0; char b; };
struct __attribute__((packed)) p17 { short s; char c; int i; };
typedef struct __attribute__((packed)) { char c; void *p; } p18;
struct p19 { char c; p18 inner[2]; };
struct __attribute__((packed)) p20 { char c; _Complex double cd; };
