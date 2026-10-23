// Types that only exist on x86-64: __int128, and bit-fields of type long wider than 32 bits.

struct i1 { char c; __int128 i; };
struct i2 { char c; unsigned __int128 u : 100; };
struct i3 { char c; __int128 a : 3; char d; };
union i4 { char c; __int128 i; };
struct __attribute__((packed)) i5 { char c; __int128 i; };
enum i6 { I6A = sizeof(__int128), I6B = _Alignof(__int128), I6C = (int)((__int128)1 << 100 >> 98) };
__int128 object_int128;
struct b21 { long l : 33; char c; };
