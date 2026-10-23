// #pragma pack: caps of the member alignment, the push/pop stack, and its interplay with bit-fields and `aligned`.

#pragma pack(1)
struct k1 { char c; int i; double d; };
struct k2 { char c; int i : 20; char d; };
struct k3 { char c; int : 0; char d; };
struct k4 { char c; int i __attribute__((aligned(8))); };
#pragma pack()
struct k5 { char c; int i; };
#pragma pack(2)
struct k6 { char c; int i; short s; long long l; };
struct k7 { char a : 4; int b : 30; };
struct k8 { char c; long long ll : 40; };
#pragma pack(push, 4)
struct k9 { char c; double d; long double ld; };
struct k10 { char c; struct k6 inner; };
#pragma pack(push, 1)
struct k11 { char c; short s; };
#pragma pack(pop)
struct k12 { char c; double d; };
#pragma pack(pop)
struct k13 { char c; double d; };
#pragma pack()
#pragma pack(8)
struct k14 { char c; double d; long double ld; };
#pragma pack(16)
struct k15 { char c; long double ld; };
#pragma pack()
#pragma pack(1)
struct __attribute__((aligned(4))) k16 { char c; int i; };
union k17 { char c; int i; double d; };
struct k18 { char a; long long : 0; char b; };
#pragma pack()
