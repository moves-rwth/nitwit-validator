#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "library_test_support.hpp"

namespace nitwit::interp::test {
namespace {

/// The result of a conversion: the value, where endptr points (offset into the string, or -1 if the function left
/// it alone or has none) and errno.
struct Conversion {
    Int128 value = 0;
    std::int64_t end = -1;
    int error = 0;
};

struct ConversionCase {
    const char* function;
    const char* text;
    int base; // 10 for atoi, atol and atoll
    Conversion expected64;
    Conversion expected32;
};

// What strtol, ... and atoi, ... return for each string, printed by a program compiled with gcc-14 -O0 -fno-builtin,
// -m64 and -m32 -msse2 -mfpmath=sse, linked to glibc 2.39 (Ubuntu 24.04). The probe was generated from the same list.
const ConversionCase conversionCases[] = {
    {"strtol", "0", 10, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtol", "42", 10, {42LL, 2, 0}, {42LL, 2, 0}},
    {"strtol", "-42", 10, {-42LL, 3, 0}, {-42LL, 3, 0}},
    {"strtol", "+7", 10, {7LL, 2, 0}, {7LL, 2, 0}},
    {"strtol", "   \t\n 12abc", 10, {12LL, 8, 0}, {12LL, 8, 0}},
    {"strtol", "abc", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtol", "", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtol", "-", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtol", "+", 0, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtol", "  ", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtol", "0x1F", 0, {31LL, 4, 0}, {31LL, 4, 0}},
    {"strtol", "0X1f", 16, {31LL, 4, 0}, {31LL, 4, 0}},
    {"strtol", "0x", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtol", "0x", 16, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtol", "-0x", 0, {0LL, 2, 0}, {0LL, 2, 0}},
    {"strtol", "0xg", 16, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtol", "1f", 16, {31LL, 2, 0}, {31LL, 2, 0}},
    {"strtol", "0777", 0, {511LL, 4, 0}, {511LL, 4, 0}},
    {"strtol", "0778", 0, {63LL, 3, 0}, {63LL, 3, 0}},
    {"strtol", "08", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtol", "0", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtol", "0b101", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtol", "0b101", 2, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtol", "101", 2, {5LL, 3, 0}, {5LL, 3, 0}},
    {"strtol", "zz", 36, {1295LL, 2, 0}, {1295LL, 2, 0}},
    {"strtol", "ZZ", 36, {1295LL, 2, 0}, {1295LL, 2, 0}},
    {"strtol", "12", 3, {5LL, 2, 0}, {5LL, 2, 0}},
    {"strtol", "2147483647", 10, {2147483647LL, 10, 0}, {2147483647LL, 10, 0}},
    {"strtol", "2147483648", 10, {2147483648LL, 10, 0}, {2147483647LL, 10, 34}},
    {"strtol", "-2147483648", 10, {-2147483648LL, 11, 0}, {-2147483648LL, 11, 0}},
    {"strtol", "-2147483649", 10, {-2147483649LL, 11, 0}, {-2147483648LL, 11, 34}},
    {"strtol", "4294967295", 10, {4294967295LL, 10, 0}, {2147483647LL, 10, 34}},
    {"strtol", "4294967296", 10, {4294967296LL, 10, 0}, {2147483647LL, 10, 34}},
    {"strtol", "-4294967295", 10, {-4294967295LL, 11, 0}, {-2147483648LL, 11, 34}},
    {"strtol", "-4294967296", 10, {-4294967296LL, 11, 0}, {-2147483648LL, 11, 34}},
    {"strtol", "9223372036854775807", 10, {9223372036854775807LL, 19, 0}, {2147483647LL, 19, 34}},
    {"strtol", "9223372036854775808", 10, {9223372036854775807LL, 19, 34}, {2147483647LL, 19, 34}},
    {"strtol", "-9223372036854775808", 10, {(-9223372036854775807LL - 1), 20, 0}, {-2147483648LL, 20, 34}},
    {"strtol", "-9223372036854775809", 10, {(-9223372036854775807LL - 1), 20, 34}, {-2147483648LL, 20, 34}},
    {"strtol", "18446744073709551615", 10, {9223372036854775807LL, 20, 34}, {2147483647LL, 20, 34}},
    {"strtol", "18446744073709551616", 10, {9223372036854775807LL, 20, 34}, {2147483647LL, 20, 34}},
    {"strtol", "-18446744073709551615", 10, {(-9223372036854775807LL - 1), 21, 34}, {-2147483648LL, 21, 34}},
    {"strtol", "-18446744073709551616", 10, {(-9223372036854775807LL - 1), 21, 34}, {-2147483648LL, 21, 34}},
    {"strtol", "99999999999999999999999", 10, {9223372036854775807LL, 23, 34}, {2147483647LL, 23, 34}},
    {"strtol", "-1", 10, {-1LL, 2, 0}, {-1LL, 2, 0}},
    {"strtol", "0xFFFFFFFFFFFFFFFF", 16, {9223372036854775807LL, 18, 34}, {2147483647LL, 18, 34}},
    {"strtol", "7fffffff", 16, {2147483647LL, 8, 0}, {2147483647LL, 8, 0}},
    {"strtol", "80000000", 16, {2147483648LL, 8, 0}, {2147483647LL, 8, 34}},
    {"strtol", "-80000000", 16, {-2147483648LL, 9, 0}, {-2147483648LL, 9, 0}},
    {"strtol", "12", 1, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtol", "12", 37, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtol", "12", -1, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtol", "1 2", 10, {1LL, 1, 0}, {1LL, 1, 0}},
    {"strtol", "--1", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtol", "- 1", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoul", "0", 10, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoul", "42", 10, {42LL, 2, 0}, {42LL, 2, 0}},
    {"strtoul", "-42", 10, {18446744073709551574ULL, 3, 0}, {4294967254LL, 3, 0}},
    {"strtoul", "+7", 10, {7LL, 2, 0}, {7LL, 2, 0}},
    {"strtoul", "   \t\n 12abc", 10, {12LL, 8, 0}, {12LL, 8, 0}},
    {"strtoul", "abc", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoul", "", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoul", "-", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoul", "+", 0, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoul", "  ", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoul", "0x1F", 0, {31LL, 4, 0}, {31LL, 4, 0}},
    {"strtoul", "0X1f", 16, {31LL, 4, 0}, {31LL, 4, 0}},
    {"strtoul", "0x", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoul", "0x", 16, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoul", "-0x", 0, {0LL, 2, 0}, {0LL, 2, 0}},
    {"strtoul", "0xg", 16, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoul", "1f", 16, {31LL, 2, 0}, {31LL, 2, 0}},
    {"strtoul", "0777", 0, {511LL, 4, 0}, {511LL, 4, 0}},
    {"strtoul", "0778", 0, {63LL, 3, 0}, {63LL, 3, 0}},
    {"strtoul", "08", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoul", "0", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoul", "0b101", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoul", "0b101", 2, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoul", "101", 2, {5LL, 3, 0}, {5LL, 3, 0}},
    {"strtoul", "zz", 36, {1295LL, 2, 0}, {1295LL, 2, 0}},
    {"strtoul", "ZZ", 36, {1295LL, 2, 0}, {1295LL, 2, 0}},
    {"strtoul", "12", 3, {5LL, 2, 0}, {5LL, 2, 0}},
    {"strtoul", "2147483647", 10, {2147483647LL, 10, 0}, {2147483647LL, 10, 0}},
    {"strtoul", "2147483648", 10, {2147483648LL, 10, 0}, {2147483648LL, 10, 0}},
    {"strtoul", "-2147483648", 10, {18446744071562067968ULL, 11, 0}, {2147483648LL, 11, 0}},
    {"strtoul", "-2147483649", 10, {18446744071562067967ULL, 11, 0}, {2147483647LL, 11, 0}},
    {"strtoul", "4294967295", 10, {4294967295LL, 10, 0}, {4294967295LL, 10, 0}},
    {"strtoul", "4294967296", 10, {4294967296LL, 10, 0}, {4294967295LL, 10, 34}},
    {"strtoul", "-4294967295", 10, {18446744069414584321ULL, 11, 0}, {1LL, 11, 0}},
    {"strtoul", "-4294967296", 10, {18446744069414584320ULL, 11, 0}, {4294967295LL, 11, 34}},
    {"strtoul", "9223372036854775807", 10, {9223372036854775807LL, 19, 0}, {4294967295LL, 19, 34}},
    {"strtoul", "9223372036854775808", 10, {9223372036854775808ULL, 19, 0}, {4294967295LL, 19, 34}},
    {"strtoul", "-9223372036854775808", 10, {9223372036854775808ULL, 20, 0}, {4294967295LL, 20, 34}},
    {"strtoul", "-9223372036854775809", 10, {9223372036854775807LL, 20, 0}, {4294967295LL, 20, 34}},
    {"strtoul", "18446744073709551615", 10, {18446744073709551615ULL, 20, 0}, {4294967295LL, 20, 34}},
    {"strtoul", "18446744073709551616", 10, {18446744073709551615ULL, 20, 34}, {4294967295LL, 20, 34}},
    {"strtoul", "-18446744073709551615", 10, {1LL, 21, 0}, {4294967295LL, 21, 34}},
    {"strtoul", "-18446744073709551616", 10, {18446744073709551615ULL, 21, 34}, {4294967295LL, 21, 34}},
    {"strtoul", "99999999999999999999999", 10, {18446744073709551615ULL, 23, 34}, {4294967295LL, 23, 34}},
    {"strtoul", "-1", 10, {18446744073709551615ULL, 2, 0}, {4294967295LL, 2, 0}},
    {"strtoul", "0xFFFFFFFFFFFFFFFF", 16, {18446744073709551615ULL, 18, 0}, {4294967295LL, 18, 34}},
    {"strtoul", "7fffffff", 16, {2147483647LL, 8, 0}, {2147483647LL, 8, 0}},
    {"strtoul", "80000000", 16, {2147483648LL, 8, 0}, {2147483648LL, 8, 0}},
    {"strtoul", "-80000000", 16, {18446744071562067968ULL, 9, 0}, {2147483648LL, 9, 0}},
    {"strtoul", "12", 1, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtoul", "12", 37, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtoul", "12", -1, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtoul", "1 2", 10, {1LL, 1, 0}, {1LL, 1, 0}},
    {"strtoul", "--1", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoul", "- 1", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoll", "0", 10, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoll", "42", 10, {42LL, 2, 0}, {42LL, 2, 0}},
    {"strtoll", "-42", 10, {-42LL, 3, 0}, {-42LL, 3, 0}},
    {"strtoll", "+7", 10, {7LL, 2, 0}, {7LL, 2, 0}},
    {"strtoll", "   \t\n 12abc", 10, {12LL, 8, 0}, {12LL, 8, 0}},
    {"strtoll", "abc", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoll", "", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoll", "-", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoll", "+", 0, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoll", "  ", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoll", "0x1F", 0, {31LL, 4, 0}, {31LL, 4, 0}},
    {"strtoll", "0X1f", 16, {31LL, 4, 0}, {31LL, 4, 0}},
    {"strtoll", "0x", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoll", "0x", 16, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoll", "-0x", 0, {0LL, 2, 0}, {0LL, 2, 0}},
    {"strtoll", "0xg", 16, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoll", "1f", 16, {31LL, 2, 0}, {31LL, 2, 0}},
    {"strtoll", "0777", 0, {511LL, 4, 0}, {511LL, 4, 0}},
    {"strtoll", "0778", 0, {63LL, 3, 0}, {63LL, 3, 0}},
    {"strtoll", "08", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoll", "0", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoll", "0b101", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoll", "0b101", 2, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoll", "101", 2, {5LL, 3, 0}, {5LL, 3, 0}},
    {"strtoll", "zz", 36, {1295LL, 2, 0}, {1295LL, 2, 0}},
    {"strtoll", "ZZ", 36, {1295LL, 2, 0}, {1295LL, 2, 0}},
    {"strtoll", "12", 3, {5LL, 2, 0}, {5LL, 2, 0}},
    {"strtoll", "2147483647", 10, {2147483647LL, 10, 0}, {2147483647LL, 10, 0}},
    {"strtoll", "2147483648", 10, {2147483648LL, 10, 0}, {2147483648LL, 10, 0}},
    {"strtoll", "-2147483648", 10, {-2147483648LL, 11, 0}, {-2147483648LL, 11, 0}},
    {"strtoll", "-2147483649", 10, {-2147483649LL, 11, 0}, {-2147483649LL, 11, 0}},
    {"strtoll", "4294967295", 10, {4294967295LL, 10, 0}, {4294967295LL, 10, 0}},
    {"strtoll", "4294967296", 10, {4294967296LL, 10, 0}, {4294967296LL, 10, 0}},
    {"strtoll", "-4294967295", 10, {-4294967295LL, 11, 0}, {-4294967295LL, 11, 0}},
    {"strtoll", "-4294967296", 10, {-4294967296LL, 11, 0}, {-4294967296LL, 11, 0}},
    {"strtoll", "9223372036854775807", 10, {9223372036854775807LL, 19, 0}, {9223372036854775807LL, 19, 0}},
    {"strtoll", "9223372036854775808", 10, {9223372036854775807LL, 19, 34}, {9223372036854775807LL, 19, 34}},
    {"strtoll",
     "-9223372036854775808",
     10,
     {(-9223372036854775807LL - 1), 20, 0},
     {(-9223372036854775807LL - 1), 20, 0}},
    {"strtoll",
     "-9223372036854775809",
     10,
     {(-9223372036854775807LL - 1), 20, 34},
     {(-9223372036854775807LL - 1), 20, 34}},
    {"strtoll", "18446744073709551615", 10, {9223372036854775807LL, 20, 34}, {9223372036854775807LL, 20, 34}},
    {"strtoll", "18446744073709551616", 10, {9223372036854775807LL, 20, 34}, {9223372036854775807LL, 20, 34}},
    {"strtoll",
     "-18446744073709551615",
     10,
     {(-9223372036854775807LL - 1), 21, 34},
     {(-9223372036854775807LL - 1), 21, 34}},
    {"strtoll",
     "-18446744073709551616",
     10,
     {(-9223372036854775807LL - 1), 21, 34},
     {(-9223372036854775807LL - 1), 21, 34}},
    {"strtoll", "99999999999999999999999", 10, {9223372036854775807LL, 23, 34}, {9223372036854775807LL, 23, 34}},
    {"strtoll", "-1", 10, {-1LL, 2, 0}, {-1LL, 2, 0}},
    {"strtoll", "0xFFFFFFFFFFFFFFFF", 16, {9223372036854775807LL, 18, 34}, {9223372036854775807LL, 18, 34}},
    {"strtoll", "7fffffff", 16, {2147483647LL, 8, 0}, {2147483647LL, 8, 0}},
    {"strtoll", "80000000", 16, {2147483648LL, 8, 0}, {2147483648LL, 8, 0}},
    {"strtoll", "-80000000", 16, {-2147483648LL, 9, 0}, {-2147483648LL, 9, 0}},
    {"strtoll", "12", 1, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtoll", "12", 37, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtoll", "12", -1, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtoll", "1 2", 10, {1LL, 1, 0}, {1LL, 1, 0}},
    {"strtoll", "--1", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoll", "- 1", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoull", "0", 10, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoull", "42", 10, {42LL, 2, 0}, {42LL, 2, 0}},
    {"strtoull", "-42", 10, {18446744073709551574ULL, 3, 0}, {18446744073709551574ULL, 3, 0}},
    {"strtoull", "+7", 10, {7LL, 2, 0}, {7LL, 2, 0}},
    {"strtoull", "   \t\n 12abc", 10, {12LL, 8, 0}, {12LL, 8, 0}},
    {"strtoull", "abc", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoull", "", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoull", "-", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoull", "+", 0, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoull", "  ", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoull", "0x1F", 0, {31LL, 4, 0}, {31LL, 4, 0}},
    {"strtoull", "0X1f", 16, {31LL, 4, 0}, {31LL, 4, 0}},
    {"strtoull", "0x", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoull", "0x", 16, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoull", "-0x", 0, {0LL, 2, 0}, {0LL, 2, 0}},
    {"strtoull", "0xg", 16, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoull", "1f", 16, {31LL, 2, 0}, {31LL, 2, 0}},
    {"strtoull", "0777", 0, {511LL, 4, 0}, {511LL, 4, 0}},
    {"strtoull", "0778", 0, {63LL, 3, 0}, {63LL, 3, 0}},
    {"strtoull", "08", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoull", "0", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoull", "0b101", 0, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoull", "0b101", 2, {0LL, 1, 0}, {0LL, 1, 0}},
    {"strtoull", "101", 2, {5LL, 3, 0}, {5LL, 3, 0}},
    {"strtoull", "zz", 36, {1295LL, 2, 0}, {1295LL, 2, 0}},
    {"strtoull", "ZZ", 36, {1295LL, 2, 0}, {1295LL, 2, 0}},
    {"strtoull", "12", 3, {5LL, 2, 0}, {5LL, 2, 0}},
    {"strtoull", "2147483647", 10, {2147483647LL, 10, 0}, {2147483647LL, 10, 0}},
    {"strtoull", "2147483648", 10, {2147483648LL, 10, 0}, {2147483648LL, 10, 0}},
    {"strtoull", "-2147483648", 10, {18446744071562067968ULL, 11, 0}, {18446744071562067968ULL, 11, 0}},
    {"strtoull", "-2147483649", 10, {18446744071562067967ULL, 11, 0}, {18446744071562067967ULL, 11, 0}},
    {"strtoull", "4294967295", 10, {4294967295LL, 10, 0}, {4294967295LL, 10, 0}},
    {"strtoull", "4294967296", 10, {4294967296LL, 10, 0}, {4294967296LL, 10, 0}},
    {"strtoull", "-4294967295", 10, {18446744069414584321ULL, 11, 0}, {18446744069414584321ULL, 11, 0}},
    {"strtoull", "-4294967296", 10, {18446744069414584320ULL, 11, 0}, {18446744069414584320ULL, 11, 0}},
    {"strtoull", "9223372036854775807", 10, {9223372036854775807LL, 19, 0}, {9223372036854775807LL, 19, 0}},
    {"strtoull", "9223372036854775808", 10, {9223372036854775808ULL, 19, 0}, {9223372036854775808ULL, 19, 0}},
    {"strtoull", "-9223372036854775808", 10, {9223372036854775808ULL, 20, 0}, {9223372036854775808ULL, 20, 0}},
    {"strtoull", "-9223372036854775809", 10, {9223372036854775807LL, 20, 0}, {9223372036854775807LL, 20, 0}},
    {"strtoull", "18446744073709551615", 10, {18446744073709551615ULL, 20, 0}, {18446744073709551615ULL, 20, 0}},
    {"strtoull", "18446744073709551616", 10, {18446744073709551615ULL, 20, 34}, {18446744073709551615ULL, 20, 34}},
    {"strtoull", "-18446744073709551615", 10, {1LL, 21, 0}, {1LL, 21, 0}},
    {"strtoull", "-18446744073709551616", 10, {18446744073709551615ULL, 21, 34}, {18446744073709551615ULL, 21, 34}},
    {"strtoull", "99999999999999999999999", 10, {18446744073709551615ULL, 23, 34}, {18446744073709551615ULL, 23, 34}},
    {"strtoull", "-1", 10, {18446744073709551615ULL, 2, 0}, {18446744073709551615ULL, 2, 0}},
    {"strtoull", "0xFFFFFFFFFFFFFFFF", 16, {18446744073709551615ULL, 18, 0}, {18446744073709551615ULL, 18, 0}},
    {"strtoull", "7fffffff", 16, {2147483647LL, 8, 0}, {2147483647LL, 8, 0}},
    {"strtoull", "80000000", 16, {2147483648LL, 8, 0}, {2147483648LL, 8, 0}},
    {"strtoull", "-80000000", 16, {18446744071562067968ULL, 9, 0}, {18446744071562067968ULL, 9, 0}},
    {"strtoull", "12", 1, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtoull", "12", 37, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtoull", "12", -1, {0LL, -1, 22}, {0LL, -1, 22}},
    {"strtoull", "1 2", 10, {1LL, 1, 0}, {1LL, 1, 0}},
    {"strtoull", "--1", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"strtoull", "- 1", 10, {0LL, 0, 0}, {0LL, 0, 0}},
    {"atoi", "0", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoi", "42", 10, {42LL, -1, 0}, {42LL, -1, 0}},
    {"atoi", "-42", 10, {-42LL, -1, 0}, {-42LL, -1, 0}},
    {"atoi", "+7", 10, {7LL, -1, 0}, {7LL, -1, 0}},
    {"atoi", "   \t\n 12abc", 10, {12LL, -1, 0}, {12LL, -1, 0}},
    {"atoi", "abc", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoi", "", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoi", "-", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoi", "  ", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoi", "2147483647", 10, {2147483647LL, -1, 0}, {2147483647LL, -1, 0}},
    {"atoi", "2147483648", 10, {-2147483648LL, -1, 0}, {2147483647LL, -1, 34}},
    {"atoi", "-2147483648", 10, {-2147483648LL, -1, 0}, {-2147483648LL, -1, 0}},
    {"atoi", "-2147483649", 10, {2147483647LL, -1, 0}, {-2147483648LL, -1, 34}},
    {"atoi", "4294967295", 10, {-1LL, -1, 0}, {2147483647LL, -1, 34}},
    {"atoi", "4294967296", 10, {0LL, -1, 0}, {2147483647LL, -1, 34}},
    {"atoi", "-4294967295", 10, {1LL, -1, 0}, {-2147483648LL, -1, 34}},
    {"atoi", "-4294967296", 10, {0LL, -1, 0}, {-2147483648LL, -1, 34}},
    {"atoi", "9223372036854775807", 10, {-1LL, -1, 0}, {2147483647LL, -1, 34}},
    {"atoi", "9223372036854775808", 10, {-1LL, -1, 34}, {2147483647LL, -1, 34}},
    {"atoi", "-9223372036854775808", 10, {0LL, -1, 0}, {-2147483648LL, -1, 34}},
    {"atoi", "-9223372036854775809", 10, {0LL, -1, 34}, {-2147483648LL, -1, 34}},
    {"atoi", "18446744073709551615", 10, {-1LL, -1, 34}, {2147483647LL, -1, 34}},
    {"atoi", "18446744073709551616", 10, {-1LL, -1, 34}, {2147483647LL, -1, 34}},
    {"atoi", "-18446744073709551615", 10, {0LL, -1, 34}, {-2147483648LL, -1, 34}},
    {"atoi", "-18446744073709551616", 10, {0LL, -1, 34}, {-2147483648LL, -1, 34}},
    {"atoi", "99999999999999999999999", 10, {-1LL, -1, 34}, {2147483647LL, -1, 34}},
    {"atoi", "-1", 10, {-1LL, -1, 0}, {-1LL, -1, 0}},
    {"atoi", "1 2", 10, {1LL, -1, 0}, {1LL, -1, 0}},
    {"atoi", "--1", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoi", "- 1", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atol", "0", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atol", "42", 10, {42LL, -1, 0}, {42LL, -1, 0}},
    {"atol", "-42", 10, {-42LL, -1, 0}, {-42LL, -1, 0}},
    {"atol", "+7", 10, {7LL, -1, 0}, {7LL, -1, 0}},
    {"atol", "   \t\n 12abc", 10, {12LL, -1, 0}, {12LL, -1, 0}},
    {"atol", "abc", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atol", "", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atol", "-", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atol", "  ", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atol", "2147483647", 10, {2147483647LL, -1, 0}, {2147483647LL, -1, 0}},
    {"atol", "2147483648", 10, {2147483648LL, -1, 0}, {2147483647LL, -1, 34}},
    {"atol", "-2147483648", 10, {-2147483648LL, -1, 0}, {-2147483648LL, -1, 0}},
    {"atol", "-2147483649", 10, {-2147483649LL, -1, 0}, {-2147483648LL, -1, 34}},
    {"atol", "4294967295", 10, {4294967295LL, -1, 0}, {2147483647LL, -1, 34}},
    {"atol", "4294967296", 10, {4294967296LL, -1, 0}, {2147483647LL, -1, 34}},
    {"atol", "-4294967295", 10, {-4294967295LL, -1, 0}, {-2147483648LL, -1, 34}},
    {"atol", "-4294967296", 10, {-4294967296LL, -1, 0}, {-2147483648LL, -1, 34}},
    {"atol", "9223372036854775807", 10, {9223372036854775807LL, -1, 0}, {2147483647LL, -1, 34}},
    {"atol", "9223372036854775808", 10, {9223372036854775807LL, -1, 34}, {2147483647LL, -1, 34}},
    {"atol", "-9223372036854775808", 10, {(-9223372036854775807LL - 1), -1, 0}, {-2147483648LL, -1, 34}},
    {"atol", "-9223372036854775809", 10, {(-9223372036854775807LL - 1), -1, 34}, {-2147483648LL, -1, 34}},
    {"atol", "18446744073709551615", 10, {9223372036854775807LL, -1, 34}, {2147483647LL, -1, 34}},
    {"atol", "18446744073709551616", 10, {9223372036854775807LL, -1, 34}, {2147483647LL, -1, 34}},
    {"atol", "-18446744073709551615", 10, {(-9223372036854775807LL - 1), -1, 34}, {-2147483648LL, -1, 34}},
    {"atol", "-18446744073709551616", 10, {(-9223372036854775807LL - 1), -1, 34}, {-2147483648LL, -1, 34}},
    {"atol", "99999999999999999999999", 10, {9223372036854775807LL, -1, 34}, {2147483647LL, -1, 34}},
    {"atol", "-1", 10, {-1LL, -1, 0}, {-1LL, -1, 0}},
    {"atol", "1 2", 10, {1LL, -1, 0}, {1LL, -1, 0}},
    {"atol", "--1", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atol", "- 1", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoll", "0", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoll", "42", 10, {42LL, -1, 0}, {42LL, -1, 0}},
    {"atoll", "-42", 10, {-42LL, -1, 0}, {-42LL, -1, 0}},
    {"atoll", "+7", 10, {7LL, -1, 0}, {7LL, -1, 0}},
    {"atoll", "   \t\n 12abc", 10, {12LL, -1, 0}, {12LL, -1, 0}},
    {"atoll", "abc", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoll", "", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoll", "-", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoll", "  ", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoll", "2147483647", 10, {2147483647LL, -1, 0}, {2147483647LL, -1, 0}},
    {"atoll", "2147483648", 10, {2147483648LL, -1, 0}, {2147483648LL, -1, 0}},
    {"atoll", "-2147483648", 10, {-2147483648LL, -1, 0}, {-2147483648LL, -1, 0}},
    {"atoll", "-2147483649", 10, {-2147483649LL, -1, 0}, {-2147483649LL, -1, 0}},
    {"atoll", "4294967295", 10, {4294967295LL, -1, 0}, {4294967295LL, -1, 0}},
    {"atoll", "4294967296", 10, {4294967296LL, -1, 0}, {4294967296LL, -1, 0}},
    {"atoll", "-4294967295", 10, {-4294967295LL, -1, 0}, {-4294967295LL, -1, 0}},
    {"atoll", "-4294967296", 10, {-4294967296LL, -1, 0}, {-4294967296LL, -1, 0}},
    {"atoll", "9223372036854775807", 10, {9223372036854775807LL, -1, 0}, {9223372036854775807LL, -1, 0}},
    {"atoll", "9223372036854775808", 10, {9223372036854775807LL, -1, 34}, {9223372036854775807LL, -1, 34}},
    {"atoll", "-9223372036854775808", 10, {(-9223372036854775807LL - 1), -1, 0}, {(-9223372036854775807LL - 1), -1, 0}},
    {"atoll",
     "-9223372036854775809",
     10,
     {(-9223372036854775807LL - 1), -1, 34},
     {(-9223372036854775807LL - 1), -1, 34}},
    {"atoll", "18446744073709551615", 10, {9223372036854775807LL, -1, 34}, {9223372036854775807LL, -1, 34}},
    {"atoll", "18446744073709551616", 10, {9223372036854775807LL, -1, 34}, {9223372036854775807LL, -1, 34}},
    {"atoll",
     "-18446744073709551615",
     10,
     {(-9223372036854775807LL - 1), -1, 34},
     {(-9223372036854775807LL - 1), -1, 34}},
    {"atoll",
     "-18446744073709551616",
     10,
     {(-9223372036854775807LL - 1), -1, 34},
     {(-9223372036854775807LL - 1), -1, 34}},
    {"atoll", "99999999999999999999999", 10, {9223372036854775807LL, -1, 34}, {9223372036854775807LL, -1, 34}},
    {"atoll", "-1", 10, {-1LL, -1, 0}, {-1LL, -1, 0}},
    {"atoll", "1 2", 10, {1LL, -1, 0}, {1LL, -1, 0}},
    {"atoll", "--1", 10, {0LL, -1, 0}, {0LL, -1, 0}},
    {"atoll", "- 1", 10, {0LL, -1, 0}, {0LL, -1, 0}},
};

class StdlibTest : public LibraryTest {
protected:
    /// Calls rand `count` times and returns the sum of the results.
    std::int64_t sumOfRand(int count) {
        std::int64_t sum = 0;
        for (int i = 0; i < count; ++i) {
            sum += static_cast<std::int64_t>(callInteger("rand", {}));
        }
        return sum;
    }

    void setErrno(int value) { writeInteger(context.objectAddress("errno"), value, ScalarType::Int); }
};

INSTANTIATE_TEST_SUITE_P(DataModels, StdlibTest, testing::Values(DataModel::ILP32, DataModel::LP64), modelName);

// ---- Termination ----

TEST_P(StdlibTest, ExitEndsTheRunWithTheStatus) {
    for (const char* function : {"exit", "_Exit", "_exit", "quick_exit"}) {
        const LibraryResult result = call(function, {intValue(3)});
        EXPECT_EQ(result.kind, LibraryResult::Kind::Exit) << function;
        EXPECT_EQ(result.status, 3) << function;
    }
    EXPECT_EQ(call("exit", {intValue(-1)}).status, -1);
}

TEST_P(StdlibTest, AbortAndVerifierErrorAbort) {
    EXPECT_TRUE(endedWith(call("abort", {}), LibraryResult::Kind::Abort, "abort"));
    EXPECT_TRUE(endedWith(call("__VERIFIER_error", {}), LibraryResult::Kind::Abort, "__VERIFIER_error"));
}

TEST_P(StdlibTest, AssertFailAbortsWithGlibcsMessage) {
    const LibraryResult result = call("__assert_fail", {pointer(string("x > 0")), pointer(string("main.c")),
                                                        integer(12, ScalarType::UnsignedInt), pointer(string("f"))});
    EXPECT_TRUE(endedWith(result, LibraryResult::Kind::Abort, "main.c:12: f: Assertion `x > 0' failed."));
    EXPECT_TRUE(endedWith(call("__assert", {pointer(string("0")), pointer(string("a.c")), intValue(7)}),
                          LibraryResult::Kind::Abort, "a.c:7: Assertion `0' failed."));
}

TEST_P(StdlibTest, AssertFailWithNullArgumentsPrintsLikeGlibc) {
    const TypedValue line = integer(12, ScalarType::UnsignedInt);
    EXPECT_TRUE(endedWith(call("__assert_fail", {pointer(string("x > 0")), pointer(string("f.c")), line, pointer(0)}),
                          LibraryResult::Kind::Abort, "f.c:12: Assertion `x > 0' failed."));
    EXPECT_TRUE(endedWith(call("__assert_fail", {pointer(0), pointer(0), line, pointer(string("main"))}),
                          LibraryResult::Kind::Abort, "(null):12: main: Assertion `(null)' failed."));
    EXPECT_TRUE(isUndefined(call("__assert_fail", {pointer(16), pointer(0), line, pointer(0)}), "null pointer"));
}

TEST_P(StdlibTest, AtomicSectionsAreNoOps) {
    EXPECT_EQ(call("__VERIFIER_atomic_begin", {}).value.kind(), value::Value::Kind::None);
    EXPECT_EQ(call("__VERIFIER_atomic_end", {}).kind, LibraryResult::Kind::Return);
}

// ---- errno ----

TEST_P(StdlibTest, ErrnoLocationIsTheErrnoObject) {
    const Address location = callPointer("__errno_location", {});
    EXPECT_NE(location, 0u);
    EXPECT_EQ(callPointer("__errno_location", {}), location);
    EXPECT_EQ(location, context.objectAddress("errno"));
    EXPECT_EQ(readInteger(location, ScalarType::Int), 0);
}

// ---- Conversions ----

TEST_P(StdlibTest, ConvertsLikeGlibc) {
    for (const ConversionCase& conversion : conversionCases) {
        SCOPED_TRACE(std::string(conversion.function) + "(\"" + conversion.text + "\", "
                     + std::to_string(conversion.base) + ")");
        const Conversion& expected = lp64() ? conversion.expected64 : conversion.expected32;
        const Address text = string(conversion.text, Region::ReadOnly);
        const Address end = buffer(8);
        writeInteger(end, 1, ScalarType::Pointer); // a sentinel, as in the probe
        setErrno(0);
        const bool isStrto = std::string_view(conversion.function).starts_with("strto");
        const LibraryResult result =
            isStrto ? call(conversion.function, {pointer(text), pointer(end), intValue(conversion.base)})
                    : call(conversion.function, {pointer(text)});
        EXPECT_EQ(returned(result), expected.value);
        EXPECT_EQ(errnoValue(), expected.error);
        const Int128 endValue = readInteger(end, ScalarType::Pointer);
        EXPECT_EQ(endValue, expected.end < 0 ? 1 : static_cast<Int128>(text + static_cast<Address>(expected.end)));
    }
}

TEST_P(StdlibTest, AnEndPointerIsOptional) {
    EXPECT_EQ(callInteger("strtol", {pointer(string(" -17")), pointer(0), intValue(10)}), -17);
}

TEST_P(StdlibTest, TheC23VersionsAcceptBinaryNumbers) {
    const Address end = buffer(8);
    const Address text = string("0b101");
    EXPECT_EQ(callInteger("__isoc23_strtol", {pointer(text), pointer(end), intValue(0)}), 5);
    EXPECT_EQ(readInteger(end, ScalarType::Pointer), static_cast<Int128>(text + 5));
    EXPECT_EQ(callInteger("__isoc23_strtoull", {pointer(string("0B11")), pointer(0), intValue(2)}), 3);
    EXPECT_EQ(callInteger("__isoc23_strtol", {pointer(text), pointer(end), intValue(10)}), 0);
    EXPECT_EQ(readInteger(end, ScalarType::Pointer), static_cast<Int128>(text + 1));
    EXPECT_EQ(callInteger("__isoc23_strtol", {pointer(string("0b")), pointer(end), intValue(0)}), 0);
}

TEST_P(StdlibTest, ConversionsReadOnlyAsFarAsTheNumberGoes) {
    EXPECT_EQ(callInteger("atoi", {pointer(bytesObject("12x"))}), 12);
    EXPECT_TRUE(isUndefined(call("atoi", {pointer(bytesObject("123"))}), "past the end"));
    EXPECT_TRUE(isUndefined(call("strtol", {pointer(0), pointer(0), intValue(10)}), "null pointer"));
    EXPECT_TRUE(
        isUndefined(call("strtol", {pointer(string("1")), pointer(string("literal", Region::ReadOnly)), intValue(10)}),
                    "read-only"));
}

// ---- Integer arithmetic ----

TEST_P(StdlibTest, AbsKeepsTheMinimum) {
    EXPECT_EQ(callInteger("abs", {intValue(-5)}), 5);
    EXPECT_EQ(callInteger("abs", {intValue(7)}), 7);
    EXPECT_EQ(callInteger("abs", {intValue(INT32_MIN)}), INT32_MIN);
    EXPECT_EQ(callInteger("labs", {integer(-5, ScalarType::Long)}), 5);
    EXPECT_EQ(callInteger("labs", {integer(INT64_MIN, ScalarType::Long)}), lp64() ? INT64_MIN : 0);
    EXPECT_EQ(callInteger("llabs", {integer(INT64_MIN, ScalarType::LongLong)}), INT64_MIN);
    EXPECT_EQ(callInteger("imaxabs", {integer(-9, ScalarType::LongLong)}), 9);
}

TEST_P(StdlibTest, DivReturnsQuotientAndRemainder) {
    const LibraryResult result = call("div", {intValue(-7), intValue(2)});
    ASSERT_EQ(result.value.kind(), value::Value::Kind::Bytes);
    const std::span<const std::byte> bytes = result.value.asBytes();
    ASSERT_EQ(bytes.size(), 8u);
    EXPECT_EQ(value::decodeInteger(bytes.first(4), format(ScalarType::Int)).signedBits(), -3);
    EXPECT_EQ(value::decodeInteger(bytes.subspan(4), format(ScalarType::Int)).signedBits(), -1);
    EXPECT_EQ(call("ldiv", {integer(9, ScalarType::Long), integer(4, ScalarType::Long)}).value.asBytes().size(),
              lp64() ? 16u : 8u);
    EXPECT_EQ(
        call("lldiv", {integer(9, ScalarType::LongLong), integer(4, ScalarType::LongLong)}).value.asBytes().size(),
        16u);
}

TEST_P(StdlibTest, DivByZeroOrOverflowIsUndefined) {
    EXPECT_TRUE(isUndefined(call("div", {intValue(1), intValue(0)}), "divides by zero"));
    EXPECT_TRUE(isUndefined(call("div", {intValue(INT32_MIN), intValue(-1)}), "overflows"));
}

// ---- rand: the values of glibc 2.39, printed natively ----

TEST_P(StdlibTest, RandWithoutSrandIsSeededWithOne) {
    EXPECT_EQ(callInteger("rand", {}), 1804289383);
    EXPECT_EQ(callInteger("rand", {}), 846930886);
    EXPECT_EQ(callInteger("rand", {}), 1681692777);
}

TEST_P(StdlibTest, RandFollowsGlibcsSequence) {
    call("srand", {integer(1, ScalarType::UnsignedInt)});
    for (const int expected : {1804289383, 846930886, 1681692777, 1714636915, 1957747793}) {
        EXPECT_EQ(callInteger("rand", {}), expected);
    }
    call("srand", {integer(42, ScalarType::UnsignedInt)});
    for (const int expected : {71876166, 708592740, 1483128881}) {
        EXPECT_EQ(callInteger("rand", {}), expected);
    }
    call("srand", {integer(123456789, ScalarType::UnsignedInt)});
    EXPECT_EQ(sumOfRand(1000), 1081145867554);
    EXPECT_EQ(callInteger("rand", {}), 372203436);
}

TEST_P(StdlibTest, SrandWithSeedZeroIsSeedOne) {
    call("srand", {integer(0, ScalarType::UnsignedInt)});
    EXPECT_EQ(callInteger("rand", {}), 1804289383);
}

TEST_P(StdlibTest, SrandWithLargeSeeds) {
    call("srand", {integer(0xFFFF'FFFF, ScalarType::UnsignedInt)});
    EXPECT_EQ(callInteger("rand", {}), 254925627);
    call("srand", {integer(0x8000'0000, ScalarType::UnsignedInt)});
    EXPECT_EQ(callInteger("rand", {}), 1336741213);
}

TEST_P(StdlibTest, RandomAndRandShareTheState) {
    call("srandom", {integer(7, ScalarType::UnsignedInt)});
    const LibraryResult first = call("random", {});
    EXPECT_EQ(first.value.bits(), value::Value::integer(1045618677, format(ScalarType::Long)).bits());
    EXPECT_EQ(callInteger("rand", {}), 1863967299);
}

TEST_P(StdlibTest, TheRandomStateIsInInterpretedMemory) {
    const Address state = context.objectAddress("__random_state");
    EXPECT_EQ(memory().find(state)->size, 132u);
    call("srand", {integer(5, ScalarType::UnsignedInt)});
    const std::vector<std::byte> seeded = readBytes(state, 132);
    call("rand", {});
    EXPECT_NE(readBytes(state, 132), seeded);
}

} // namespace
} // namespace nitwit::interp::test
