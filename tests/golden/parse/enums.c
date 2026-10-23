// Enumerations: values, references to earlier constants, forward declarations.
enum color { RED, GREEN = 4, BLUE = GREEN + 1, };
enum { ANONYMOUS = -1 } value;
enum later;
enum later *forward;
enum later { L1 };
int table[BLUE];
