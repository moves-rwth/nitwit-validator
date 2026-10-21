// Enumerations: values (C17 6.7.2.2) and the integer type GCC chooses, also for packed enumerations and values
// outside the range of int.
enum unsigned_values { A, B, C = 10, D };
enum signed_values { NEGATIVE = -1, POSITIVE = 1 };
enum large { SMALL = 1, BIG = 0x100000000 };
enum __attribute__((packed)) tiny { T0, T1 = 200 };
enum derived { E0 = sizeof(enum tiny), E1 = C * 2, E2 = 'x', E3 };

int use(enum unsigned_values v) {
    return v + D;
}
