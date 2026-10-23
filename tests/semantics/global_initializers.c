// Objects with static storage duration: zero initialization, constant initializers with addresses, tentative
// definitions, and the order-independence of file-scope initialization.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

struct Config {
    const char *name;
    int limits[3];
    double ratio;
    struct Config *next;
};

int zeroInt;
double zeroDouble;
int *zeroPointer;
struct Config zeroConfig;
int tentative;
int tentative; // a second tentative definition of the same object
int table[5] = {1, 2, 3};
int *middle = &table[2];
int *pastEnd = table + 5;
const char *greeting = "hello";
char letters[] = "xyz";
struct Config second = {"second", {4, 5, 6}, 0.25, 0};
struct Config first = {"first", {1}, 1.5, &second};
long long big = 1LL << 40;
unsigned char bytes[4] = {255, 128};
int computed = sizeof(struct Config) > 8 ? 100 : 200;
double fromInt = 7 / 2;

int main(void) {
    printf("%d %g %d %d %d\n", zeroInt, zeroDouble, zeroPointer == 0, zeroConfig.limits[2], zeroConfig.name == 0);
    printf("%d %d %d %d\n", tentative, table[4], *middle, (int)(pastEnd - middle));
    printf("%s %s %u\n", greeting, letters, (unsigned)sizeof letters);
    printf("%s %d %d %g %s\n", first.name, first.limits[0], first.limits[1], first.ratio, first.next->name);
    printf("%d %g\n", first.next->limits[2], first.next->ratio);
    printf("%lld %d %d %d %d\n", big, bytes[0], bytes[1], bytes[2], computed);
    printf("%g\n", fromInt);

    int k = __VERIFIER_nondet_int();
    table[k] = 50;
    printf("%d %d\n", *middle, middle[-1]);
    tentative += k;
    printf("%d\n", tentative);
    return 0;
}
