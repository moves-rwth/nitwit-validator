// The input contract (tools/difftest/README.md, "Harness") for the less common nondet functions:
// __VERIFIER_nondet_memory takes one line per byte, pointers take an address, the kernel types convert like their x86
// typedefs, uint128 (64-bit targets only) reads modulo 2^128, and exhausted inputs give 0, logged as "default".
int printf(const char *, ...);
typedef __typeof__(sizeof 0) size_t;
void __VERIFIER_nondet_memory(void *, size_t);
void *__VERIFIER_nondet_pointer(void);
char *__VERIFIER_nondet_pchar(void);
size_t __VERIFIER_nondet_size_t(void);
unsigned char __VERIFIER_nondet_u8(void);
unsigned short __VERIFIER_nondet_u16(void);
unsigned int __VERIFIER_nondet_u32(void);
long long __VERIFIER_nondet_loff_t(void);
unsigned long __VERIFIER_nondet_sector_t(void);
unsigned long long __VERIFIER_nondet_ulonglong(void);
int __VERIFIER_nondet_int(void);
#ifdef __SIZEOF_INT128__
unsigned __int128 __VERIFIER_nondet_uint128(void);
#endif

struct Record {
    unsigned char tag;
    unsigned short value;
    int count;
};

int main(void) {
    unsigned char bytes[4];
    __VERIFIER_nondet_memory(bytes, sizeof bytes);
    printf("%u %u %u %u\n", (unsigned)bytes[0], (unsigned)bytes[1], (unsigned)bytes[2], (unsigned)bytes[3]);

    void *null = __VERIFIER_nondet_pointer();
    void *page = __VERIFIER_nondet_pointer();
    char *text = __VERIFIER_nondet_pchar();
    printf("%d %d %d %lu\n", null == 0, page != 0, text == 0, (unsigned long)page);

    size_t size = __VERIFIER_nondet_size_t();
    unsigned char u8 = __VERIFIER_nondet_u8();
    unsigned short u16 = __VERIFIER_nondet_u16();
    unsigned int u32 = __VERIFIER_nondet_u32();
    long long offset = __VERIFIER_nondet_loff_t();
    unsigned long sector = __VERIFIER_nondet_sector_t();
    printf("%llu %u %u %u %lld %lu\n", (unsigned long long)size, (unsigned)u8, (unsigned)u16, u32, offset, sector);

#ifdef __SIZEOF_INT128__
    unsigned __int128 wide = __VERIFIER_nondet_uint128();
    printf("%016llx%016llx\n", (unsigned long long)(wide >> 64), (unsigned long long)wide);
#else
    unsigned long long wide = __VERIFIER_nondet_ulonglong();
    printf("%016llx\n", wide);
#endif

    // The inputs are exhausted from here on.
    int defaulted = __VERIFIER_nondet_int();
    void *defaultPointer = __VERIFIER_nondet_pointer();
    struct Record record;
    __VERIFIER_nondet_memory(&record, sizeof record);
    printf("%d %d %u %u %d\n", defaulted, defaultPointer == 0, (unsigned)record.tag, (unsigned)record.value,
           record.count);
    return 0;
}
