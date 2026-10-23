// The types of literals (C17 6.4.4, 6.4.5) on i386, where long has 32 bits and wchar_t is long.
void literals(void) {
    2147483647;
    2147483648;
    0x7fffffff;
    0x80000000;
    0xffffffffffffffff;
    10u;
    10l;
    10ul;
    10ll;
    10ull;
    'a';
    L'a';
    u'a';
    U'a';
    1.5f;
    1.5;
    1.5L;
    "text";
    L"wide";
    u"utf16";
    U"utf32";
    u8"utf8";
}
