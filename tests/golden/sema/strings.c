// String literals: initializers of character arrays (C17 6.7.9p14), pointers to them, and __func__.
char text[] = "hello";
char braced[] = { "hi" };
char exact[3] = "abc";
char padded[8] = "ab";
const char *pointer = "constant";
char names[][4] = { "ab", "cde" };
struct record { char name[8]; int id; } r = { "nitwit", 4 };
int wide[] = L"wide";

const char *name(void) {
    return __func__;
}
