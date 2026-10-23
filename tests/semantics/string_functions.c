// String functions of <string.h>; strcmp results are reduced to their sign, which is all the standard defines.
int printf(const char *, ...);
typedef __typeof__(sizeof 0) size_t;
size_t strlen(const char *);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
char *strcat(char *, const char *);
char *strncat(char *, const char *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
int __VERIFIER_nondet_int(void);

int sign(int value) {
    return (value > 0) - (value < 0);
}

int main(void) {
    char buffer[32];
    printf("%u %u\n", (unsigned)strlen("hello"), (unsigned)strlen(""));
    strcpy(buffer, "abc");
    strcat(buffer, "def");
    printf("%s %u\n", buffer, (unsigned)strlen(buffer));
    strncat(buffer, "ghijkl", 2);
    printf("%s\n", buffer);

    char padded[8];
    strncpy(padded, "xy", sizeof padded);
    printf("%s %d %d\n", padded, padded[2], padded[7]);
    char truncated[4];
    strncpy(truncated, "abcdef", 3);
    truncated[3] = '\0';
    printf("%s\n", truncated);

    printf("%d %d %d\n", sign(strcmp("abc", "abd")), sign(strcmp("abc", "abc")), sign(strcmp("b", "abc")));
    printf("%d %d\n", sign(strcmp("abc", "ab")), sign(strcmp("\xff", "a")));
    printf("%d %d\n", sign(strncmp("abcx", "abcy", 3)), sign(strncmp("abcx", "abcy", 4)));

    const char *text = "the quick brown fox";
    printf("%s|%s|%s\n", strchr(text, 'q'), strrchr(text, 'o'), strstr(text, "brown"));
    printf("%d %d %d\n", strchr(text, 'z') == 0, strstr(text, "cat") == 0, (int)(strchr(text, '\0') - text));
    printf("%d\n", (int)(strstr(text, "") - text));

    int k = __VERIFIER_nondet_int();
    char digits[16];
    int n = 0;
    do {
        digits[n++] = (char)('0' + k % 10);
        k /= 10;
    } while (k > 0);
    digits[n] = '\0';
    printf("%s %u\n", digits, (unsigned)strlen(digits));
    return 0;
}
