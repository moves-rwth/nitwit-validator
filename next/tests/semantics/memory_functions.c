// Memory functions of <string.h>: memset, memcpy, memmove with overlap, memcmp (sign only), memchr.
int printf(const char *, ...);
void *memset(void *, int, unsigned long);
void *memcpy(void *, const void *, unsigned long);
void *memmove(void *, const void *, unsigned long);
int memcmp(const void *, const void *, unsigned long);
void *memchr(const void *, int, unsigned long);
int __VERIFIER_nondet_int(void);

struct Record {
    int id;
    char name[8];
    double weight;
};

int sign(int value) {
    return (value > 0) - (value < 0);
}

void show(const char *label, const unsigned char *bytes, int count) {
    printf("%s:", label);
    for (int i = 0; i < count; ++i) {
        printf(" %d", bytes[i]);
    }
    printf("\n");
}

int main(void) {
    unsigned char bytes[10];
    memset(bytes, 0xAB, sizeof bytes);
    memset(bytes + 2, 300, 3); // the value is converted to unsigned char
    show("memset", bytes, 10);

    int numbers[5] = {1, 2, 3, 4, 5};
    int copy[5];
    memcpy(copy, numbers, sizeof numbers);
    printf("%d %d\n", copy[0], copy[4]);

    memmove(numbers + 1, numbers, 3 * sizeof(int));
    printf("%d %d %d %d %d\n", numbers[0], numbers[1], numbers[2], numbers[3], numbers[4]);
    char text[] = "abcdefgh";
    memmove(text, text + 2, 5);
    printf("%s\n", text);

    printf("%d %d %d\n", sign(memcmp("abc", "abd", 3)), sign(memcmp("abc", "abd", 2)),
           sign(memcmp("\x80", "\x01", 1)));

    const char *haystack = "find the needle";
    const char *found = memchr(haystack, 'n', 15);
    printf("%s %d\n", found, memchr(haystack, 'z', 15) == 0);

    struct Record a = {7, "anna", 61.5};
    struct Record b;
    memcpy(&b, &a, sizeof a);
    b.id = 8;
    printf("%d %s %g %d %s %g\n", a.id, a.name, a.weight, b.id, b.name, b.weight);

    // Integers built from bytes (little endian).
    unsigned int word;
    unsigned char source[4] = {0x78, 0x56, 0x34, 0x12};
    memcpy(&word, source, 4);
    printf("%x\n", word);
    double d = 1.0;
    unsigned long long bits;
    memcpy(&bits, &d, sizeof d);
    printf("%llx\n", bits);

    int k = __VERIFIER_nondet_int();
    int zeros[8];
    memset(zeros, 0, sizeof zeros);
    memset(zeros, k, 1);
    printf("%d %d\n", zeros[0], zeros[1]);
    return 0;
}
