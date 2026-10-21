// String literals (C17 6.4.5): type and size, concatenation, escape sequences, embedded null characters, arrays
// initialized from literals.
int printf(const char *, ...);
unsigned long strlen(const char *);
int __VERIFIER_nondet_int(void);

int main(void) {
    printf("%u %u %u\n", (unsigned)sizeof("abc"), (unsigned)sizeof(""), (unsigned)strlen("abc"));
    printf("%s|%u\n", "con" "cat" "enated", (unsigned)sizeof("con" "cat"));
    const char *escapes = "tab\there \"quoted\" back\\slash \x41\102\x43";
    printf("%s\n", escapes);
    printf("%u %u\n", (unsigned)sizeof("a\0b"), (unsigned)strlen("a\0b"));
    printf("%d %d %d\n", "hello"[1], *"hello", "hello"[5]);
    printf("%d %d\n", "\xff"[0], "\377"[0] == '\377');
    printf("%u\n", (unsigned)sizeof("\x41" "1")); // "A1": a hexadecimal escape ends with its literal

    char array[] = "mutable";
    array[0] = 'M';
    printf("%s %u\n", array, (unsigned)sizeof array);
    char exact[3] = "abc"; // no room for the terminator, so there is none
    printf("%c%c%c %u\n", exact[0], exact[1], exact[2], (unsigned)sizeof exact);
    char longer[8] = "ab";
    printf("%d %d %d\n", longer[2], longer[7], (int)strlen(longer));

    const char *lines[] = {"first", "second", "third"};
    int k = __VERIFIER_nondet_int();
    printf("%s %c %u\n", lines[k], lines[k][0], (unsigned)strlen(lines[k]));
    const char *p = "pointer";
    p += k;
    printf("%s\n", p);
    printf("%s\n", "multi\n"
                   "line");
    return 0;
}
