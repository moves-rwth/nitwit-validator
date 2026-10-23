// Integer promotions and the usual arithmetic conversions (C17 6.3.1.1, 6.3.1.8).
char c;
unsigned char uc;
short s;
unsigned short us;
int i;
unsigned u;
long l;
unsigned long ul;
long long ll;
float f;
double d;
long double ld;
_Bool b;
enum color { RED, GREEN } e;

void arithmetic(void) {
    c + c;
    uc * s;
    i + u;
    l - u;
    ll / ul;
    i * f;
    f + d;
    ld - ll;
    b + b;
    e + 1;
    -us;
    ~c;
    c << l;
    u >> 3;
    i % c;
    !d;
    i && f;
    d < l;
}
