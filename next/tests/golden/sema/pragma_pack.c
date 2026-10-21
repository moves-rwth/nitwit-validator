// #pragma pack caps the alignment of members, also of bit-fields, which may then straddle their units.
#pragma pack(push, 1)
struct one { char c; int i; double d; };
struct one_bits { char a : 4; int b : 30; };
#pragma pack(2)
struct two { char c; int i; long long l; };
#pragma pack(pop)
struct natural { char c; int i; double d; };
