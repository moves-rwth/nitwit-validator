// Types that depend on the data model, on i386: long and pointers have 32 bits (long + unsigned int is unsigned
// long, long long + unsigned long is long long), size_t is unsigned int and ptrdiff_t int.
long l;
unsigned long ul;
unsigned int u;
long long ll;
int *p;
char buf[16];
struct mixed { char c; double d; long long ll; long double ld; };

unsigned long models(void) {
    l + u;
    ll + ul;
    p - p;
    (long long)p;
    __atomic_fetch_add(&p, 1, 0);
    return sizeof(struct mixed) + __builtin_object_size(buf, 0);
}
