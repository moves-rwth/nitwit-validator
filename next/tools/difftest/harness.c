/* Native implementation of the SV-COMP functions for differential testing, see README.md.
 *
 * The harness is linked to the program under test. Every function is a weak symbol, so that a definition in the
 * program wins. Nondeterministic values come from the file named by NITWIT_INPUTS, one value per line, and every value
 * handed out is appended to the file named by NITWIT_INPUT_LOG. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#define HARNESS_WEAK __attribute__((weak))

enum { exitError = 97, exitAssume = 98, exitBadInput = 99 };

/* The longest input line, without the line break. */
enum { maxLineLength = 255 };

/* Integer input lines are read modulo 2^N with the widest N available, so that every return type, including
 * unsigned __int128 on 64-bit targets, gets its value by a plain conversion. */
#ifdef __SIZEOF_INT128__
__extension__ typedef unsigned __int128 WideUnsigned;
#else
typedef unsigned long long WideUnsigned;
#endif

static FILE *inputsFile;
static int inputsOpened;
static long inputsLine;
static FILE *inputLog;
static int inputLogOpened;

/* Unbuffered stdout keeps the output of runs that end by a signal or by _exit. A process that is not dumpable writes
 * no core dump when it aborts or crashes; WSL's core_pattern helper would collect one despite RLIMIT_CORE. */
__attribute__((constructor)) static void harnessInit(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    prctl(PR_SET_DUMPABLE, 0);
}

/* Ends the run immediately, without atexit handlers, after flushing all streams. */
static void endRun(int code) {
    fflush(NULL);
    _exit(code);
}

static void badInput(const char *function, const char *message) {
    fprintf(stderr, "NITWIT-DIFFTEST: bad input: %s, line %ld of NITWIT_INPUTS: %s\n", function, inputsLine, message);
    endRun(exitBadInput);
}

/* Reads the next line of the inputs file into line (maxLineLength + 1 bytes), without the line break (\n or \r\n).
 * Returns 0 if the file is exhausted, or if NITWIT_INPUTS is not set. */
static int nextLine(const char *function, char *line) {
    if (!inputsOpened) {
        inputsOpened = 1;
        const char *path = getenv("NITWIT_INPUTS");
        if (path != NULL && path[0] != '\0') {
            inputsFile = fopen(path, "r");
            if (inputsFile == NULL) {
                badInput(function, "cannot open the file");
            }
        }
    }
    char buffer[maxLineLength + 3]; /* room for \r, \n and the terminator */
    if (inputsFile == NULL || fgets(buffer, sizeof buffer, inputsFile) == NULL) {
        return 0;
    }
    ++inputsLine;
    size_t length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n') {
        buffer[--length] = '\0';
    } else if (!feof(inputsFile)) {
        badInput(function, "line too long");
    }
    if (length > 0 && buffer[length - 1] == '\r') {
        buffer[--length] = '\0';
    }
    if (length > maxLineLength) {
        badInput(function, "line too long");
    }
    memcpy(line, buffer, length + 1);
    return 1;
}

static int isDecimalInteger(const char *text) {
    if (*text == '+' || *text == '-') {
        ++text;
    }
    if (*text == '\0') {
        return 0;
    }
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9') {
            return 0;
        }
    }
    return 1;
}

static int isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* A C99 hexadecimal floating constant as %a prints it: [+-]0x<hex>[.<hex>]p[+-]<decimal>. */
static int isHexFloat(const char *text) {
    if (*text == '+' || *text == '-') {
        ++text;
    }
    if (text[0] != '0' || (text[1] != 'x' && text[1] != 'X') || !isHexDigit(text[2])) {
        return 0;
    }
    text += 2;
    while (isHexDigit(*text)) {
        ++text;
    }
    if (*text == '.') {
        ++text;
        while (isHexDigit(*text)) {
            ++text;
        }
    }
    if (*text != 'p' && *text != 'P') {
        return 0;
    }
    return isDecimalInteger(text + 1);
}

/* The spellings of infinities and NaNs that %a prints. */
static int isSpecialFloat(const char *text) {
    if (*text == '-') {
        ++text;
    }
    return strcmp(text, "inf") == 0 || strcmp(text, "nan") == 0;
}

/* Appends "<function> <value>[ default]" to the input log. */
static void logValue(const char *function, const char *value, int isDefault) {
    if (!inputLogOpened) {
        inputLogOpened = 1;
        const char *path = getenv("NITWIT_INPUT_LOG");
        if (path != NULL && path[0] != '\0') {
            inputLog = fopen(path, "a");
        }
    }
    if (inputLog != NULL) {
        fprintf(inputLog, "%s %s%s\n", function, value, isDefault ? " default" : "");
        fflush(inputLog);
    }
}

/* Reads a decimal integer line ([+-]digits) modulo 2^N. Returns 0 (and value 0) if the inputs are exhausted. */
static int readInteger(const char *function, WideUnsigned *value) {
    char line[maxLineLength + 1];
    *value = 0;
    if (!nextLine(function, line)) {
        return 0;
    }
    if (!isDecimalInteger(line)) {
        badInput(function, "expected a decimal integer");
    }
    const char *p = line[0] == '+' || line[0] == '-' ? line + 1 : line;
    for (; *p != '\0'; ++p) {
        unsigned digit = (unsigned)(*p - '0');
        *value = *value * 10u + digit;
    }
    if (line[0] == '-') {
        *value = -*value;
    }
    return 1;
}

/* Reads a floating-point line: a decimal integer, a hexadecimal floating constant, inf, -inf, nan or -nan. strtod
 * converts each of them exactly or, for long decimal integers, correctly rounded. Returns 0 (and value 0) if the inputs
 * are exhausted. */
static int readFloating(const char *function, double *value) {
    char line[maxLineLength + 1];
    *value = 0;
    if (!nextLine(function, line)) {
        return 0;
    }
    if (!isDecimalInteger(line) && !isHexFloat(line) && !isSpecialFloat(line)) {
        badInput(function, "expected a decimal integer, a hexadecimal float, inf or nan");
    }
    *value = strtod(line, NULL);
    return 1;
}

static void logSigned(const char *function, long long value, int fromFile) {
    char text[32];
    snprintf(text, sizeof text, "%lld", value);
    logValue(function, text, !fromFile);
}

static void logUnsigned(const char *function, unsigned long long value, int fromFile) {
    char text[32];
    snprintf(text, sizeof text, "%llu", value);
    logValue(function, text, !fromFile);
}

static void logFloating(const char *function, double value, int fromFile) {
    char text[64];
    snprintf(text, sizeof text, "%a", value);
    logValue(function, text, !fromFile);
}

/* Each nondet function converts the line to its return type as a C cast does (GCC and Clang wrap out-of-range
 * integers modulo 2^N) and logs the converted value. */
#define HARNESS_NONDET_SIGNED(suffix, type)                                                                            \
    HARNESS_WEAK type __VERIFIER_nondet_##suffix(void) {                                                             \
        WideUnsigned raw;                                                                                              \
        int fromFile = readInteger("__VERIFIER_nondet_" #suffix, &raw);                                                \
        type value = (type)raw;                                                                                        \
        logSigned("__VERIFIER_nondet_" #suffix, (long long)value, fromFile);                                           \
        return value;                                                                                                  \
    }

#define HARNESS_NONDET_UNSIGNED(suffix, type)                                                                          \
    HARNESS_WEAK type __VERIFIER_nondet_##suffix(void) {                                                             \
        WideUnsigned raw;                                                                                              \
        int fromFile = readInteger("__VERIFIER_nondet_" #suffix, &raw);                                                \
        type value = (type)raw;                                                                                        \
        logUnsigned("__VERIFIER_nondet_" #suffix, (unsigned long long)value, fromFile);                                \
        return value;                                                                                                  \
    }

#define HARNESS_NONDET_POINTER(suffix, type)                                                                           \
    HARNESS_WEAK type __VERIFIER_nondet_##suffix(void) {                                                             \
        WideUnsigned raw;                                                                                              \
        int fromFile = readInteger("__VERIFIER_nondet_" #suffix, &raw);                                                \
        uintptr_t address = (uintptr_t)raw;                                                                            \
        logUnsigned("__VERIFIER_nondet_" #suffix, (unsigned long long)address, fromFile);                              \
        return (type)address;                                                                                          \
    }

#define HARNESS_NONDET_FLOATING(suffix, type)                                                                          \
    HARNESS_WEAK type __VERIFIER_nondet_##suffix(void) {                                                             \
        double raw;                                                                                                    \
        int fromFile = readFloating("__VERIFIER_nondet_" #suffix, &raw);                                               \
        type value = (type)raw;                                                                                        \
        logFloating("__VERIFIER_nondet_" #suffix, (double)value, fromFile);                                            \
        return value;                                                                                                  \
    }

/* The types follow the declarations in sv-benchmarks; the Linux kernel typedefs are those of x86. */
HARNESS_NONDET_SIGNED(char, char)
HARNESS_NONDET_SIGNED(short, short)
HARNESS_NONDET_SIGNED(int, int)
HARNESS_NONDET_SIGNED(long, long)
HARNESS_NONDET_SIGNED(longlong, long long)
HARNESS_NONDET_SIGNED(loff_t, long long)
HARNESS_NONDET_UNSIGNED(bool, _Bool)
HARNESS_NONDET_UNSIGNED(uchar, unsigned char)
HARNESS_NONDET_UNSIGNED(ushort, unsigned short)
HARNESS_NONDET_UNSIGNED(uint, unsigned int)
HARNESS_NONDET_UNSIGNED(unsigned, unsigned int)
HARNESS_NONDET_UNSIGNED(ulong, unsigned long)
HARNESS_NONDET_UNSIGNED(ulonglong, unsigned long long)
HARNESS_NONDET_UNSIGNED(size_t, size_t)
HARNESS_NONDET_UNSIGNED(sector_t, unsigned long)
HARNESS_NONDET_UNSIGNED(u8, unsigned char)
HARNESS_NONDET_UNSIGNED(u16, unsigned short)
HARNESS_NONDET_UNSIGNED(u32, unsigned int)
HARNESS_NONDET_UNSIGNED(pthread_t, unsigned long)
HARNESS_NONDET_POINTER(pointer, void *)
HARNESS_NONDET_POINTER(pchar, char *)
HARNESS_NONDET_POINTER(charp, char *)
HARNESS_NONDET_FLOATING(float, float)
HARNESS_NONDET_FLOATING(double, double)

#ifdef __SIZEOF_INT128__
/* Logged in decimal, since printf has no conversion for 128-bit integers. */
HARNESS_WEAK WideUnsigned __VERIFIER_nondet_uint128(void) {
    WideUnsigned value;
    int fromFile = readInteger("__VERIFIER_nondet_uint128", &value);
    char digits[48];
    char *p = digits + sizeof digits - 1;
    *p = '\0';
    WideUnsigned rest = value;
    do {
        *--p = (char)('0' + (int)(rest % 10));
        rest /= 10;
    } while (rest != 0);
    logValue("__VERIFIER_nondet_uint128", p, !fromFile);
    return value;
}
#endif

/* Fills the memory byte by byte; every byte takes one line and has its own log entry. */
HARNESS_WEAK void __VERIFIER_nondet_memory(void *destination, size_t size) {
    unsigned char *bytes = destination;
    for (size_t i = 0; i < size; ++i) {
        WideUnsigned raw;
        int fromFile = readInteger("__VERIFIER_nondet_memory", &raw);
        bytes[i] = (unsigned char)raw;
        logUnsigned("__VERIFIER_nondet_memory", bytes[i], fromFile);
    }
}

static void errorReached(const char *function) {
    fprintf(stderr, "NITWIT-DIFFTEST: error %s\n", function);
    endRun(exitError);
}

HARNESS_WEAK void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    (void)assertion;
    (void)file;
    (void)line;
    (void)function;
    errorReached("__assert_fail");
}

HARNESS_WEAK void __VERIFIER_error(void) {
    errorReached("__VERIFIER_error");
}

/* Used only by programs that declare reach_error without defining it. */
HARNESS_WEAK void reach_error(void) {
    errorReached("reach_error");
}

HARNESS_WEAK void __VERIFIER_assume(int condition) {
    if (!condition) {
        fprintf(stderr, "NITWIT-DIFFTEST: assume\n");
        endRun(exitAssume);
    }
}

/* Atomic sections only matter with threads; programs that create threads are not compared (README.md). */
HARNESS_WEAK void __VERIFIER_atomic_begin(void) {
}

HARNESS_WEAK void __VERIFIER_atomic_end(void) {
}
