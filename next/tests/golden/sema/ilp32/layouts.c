// Record layouts on i386 (nitwit-dump -32): padding, bit-fields, packed and aligned records and members, flexible arrays, unions.
struct padded { char c; int i; short s; long l; };
struct bits { unsigned a : 3; unsigned b : 5; unsigned : 0; char c; long long d : 40; int e : 25; };
struct __attribute__((packed)) packed { char c; int i; short s; };
struct member_packed { char c; int i __attribute__((packed)); long l; };
struct __attribute__((aligned(16))) aligned { char c; };
struct member_aligned { char c; int i __attribute__((aligned(8))); };
struct flexible { int n; char data[]; };
struct zero { long l; char none[0]; };
union mixed { char c[5]; int i; double d; };
struct nested { char tag; struct padded inner; union mixed u; struct { int x; }; };
