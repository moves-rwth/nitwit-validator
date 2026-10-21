// Enumerations: GCC's choice of the compatible integer type, packed enumerations, and the C23 rule that all constants
// of an enumeration with a value outside int have the enumerated type.

enum e1 { E1A, E1B, E1C };
enum e2 { E2A = -1, E2B = 1 };
enum e3 { E3A = 0x7fffffff };
enum e4 { E4A = 0x80000000 };
enum e5 { E5A = -1, E5B = 0x80000000 };
enum e6 { E6A = 0xffffffffffffffffULL };
enum e7 { E7A = -0x7fffffffffffffffLL - 1, E7B = 1 };
enum __attribute__((packed)) e8 { E8A, E8B = 200 };
enum __attribute__((packed)) e9 { E9A = -1, E9B = 100 };
enum __attribute__((packed)) e10 { E10A = 300 };
enum __attribute__((packed)) e11 { E11A = -129 };
enum __attribute__((packed)) e12 { E12A = 0x10000 };
enum e13 { E13A = 10, E13B, E13C = E13A + E13B, E13D };
enum e14 { E14A = 'a', E14B = sizeof(enum e13), E14C = (char)300 };
enum e15 { E15A = 0x7fffffff, E15B = 0x100000000 };
enum e16 { E16A = 0xfffffffe, E16B };
struct with_enums { char c; enum e8 small; enum e6 big; enum e9 tiny; };
