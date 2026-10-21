// Literal forms.
int i[] = { 0, 42, 0x2A, 052, 0b101010, 42u, 42l, 42ul, 42ll, 42ULL, 18446744073709551615u };
double d[] = { 1.0, 1e3, 1.5f, 2.5L, 0x1p4, .5, 3. };
int ch[] = { 'a', '\n', '\0', '\377', L'w', u'x', U'y', 'ab' };
char *s1 = "plain";
char *s2 = "con" "cat" "enated";
char *s3 = "esc\t\"q\"\\";
int *w = L"wide";
char *u8 = u8"utf8";
