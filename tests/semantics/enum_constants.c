// Enumerations (C17 6.7.2.2): implicit and explicit values, constants of type int, enums in switch and in structs.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

enum Color { Red, Green, Blue };
enum Level { Low = -1, Medium, High = 10, Highest, Alias = Medium };
enum Flags { Read = 1 << 0, Write = 1 << 1, Execute = 1 << 2, All = Read | Write | Execute };
enum { Anonymous = 0x7FFFFFFF };

struct Item {
    enum Color color;
    enum Level level;
};

const char *colorName(enum Color color) {
    switch (color) {
    case Red:
        return "red";
    case Green:
        return "green";
    case Blue:
        return "blue";
    }
    return "unknown";
}

int main(void) {
    printf("%d %d %d\n", Red, Green, Blue);
    printf("%d %d %d %d %d\n", Low, Medium, High, Highest, Alias);
    printf("%d %d\n", All, Read | Execute);
    printf("%d %u\n", Anonymous, (unsigned)sizeof(Anonymous));
    printf("%u %u\n", (unsigned)sizeof(enum Color), (unsigned)sizeof(Red));
    printf("%d\n", Low < 0);

    enum Color color = Blue;
    color = (enum Color)(color - 1);
    printf("%s\n", colorName(color));
    struct Item item = {Green, High};
    printf("%s %d\n", colorName(item.color), item.level + 1);

    int k = __VERIFIER_nondet_int();
    enum Color fromInput = (enum Color)k;
    printf("%s %d\n", colorName(fromInput), fromInput == Blue);
    int permissions = Read;
    permissions |= k > 1 ? Write : 0;
    printf("%d %d\n", permissions, (permissions & Write) != 0);
    // An enum constant may be used in later constant expressions, also inside the same enum.
    enum { Base = 100, Next = Base + 5, Size = sizeof(int) * Next };
    printf("%d %d\n", Next, Size);
    return 0;
}
