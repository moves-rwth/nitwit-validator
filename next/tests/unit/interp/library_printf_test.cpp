#include <cfloat>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "library_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using namespace std::string_literals;
using namespace std::string_view_literals;
using T = ScalarType;

constexpr double inf = std::numeric_limits<double>::infinity();
constexpr double nan = std::numeric_limits<double>::quiet_NaN();

/// An argument of a printf case. Integers become values of `type` in the data model of the test, so that the same
/// case covers the truncation a native -m32 build does.
struct Argument {
    enum class Kind : std::uint8_t { Integer, Size, SignedSize, PtrDiff, Double, LongDouble, String, Pointer };
    Kind kind = Kind::Integer;
    ScalarType type = ScalarType::Int;
    Int128 integer = 0;
    double floating = 0;
    long double longDouble = 0;
    const char* text = nullptr;
};

Argument integerArgument(Argument::Kind kind, ScalarType type, Int128 value) {
    Argument argument;
    argument.kind = kind;
    argument.type = type;
    argument.integer = value;
    return argument;
}

Argument I(ScalarType type, Int128 value) {
    return integerArgument(Argument::Kind::Integer, type, value);
}

Argument Z(Int128 value) {
    return integerArgument(Argument::Kind::Size, ScalarType::Int, value);
}

Argument SZ(Int128 value) {
    return integerArgument(Argument::Kind::SignedSize, ScalarType::Int, value);
}

Argument PD(Int128 value) {
    return integerArgument(Argument::Kind::PtrDiff, ScalarType::Int, value);
}

Argument P(Int128 address) {
    return integerArgument(Argument::Kind::Pointer, ScalarType::Pointer, address);
}

Argument D(double value) {
    Argument argument;
    argument.kind = Argument::Kind::Double;
    argument.floating = value;
    return argument;
}

Argument LD(long double value) {
    Argument argument;
    argument.kind = Argument::Kind::LongDouble;
    argument.longDouble = value;
    return argument;
}

Argument S(const char* text) {
    Argument argument;
    argument.kind = Argument::Kind::String;
    argument.text = text;
    return argument;
}

struct PrintfCase {
    const char* format;
    std::vector<Argument> arguments;
    std::string_view expected64;
    std::string_view expected32;
};

// What snprintf(buffer, 4096, format, arguments...) writes, printed by a program compiled with gcc-14 -O0
// -fno-builtin, -m64 and -m32 -msse2 -mfpmath=sse (as tools/difftest compiles), linked to glibc 2.39 (Ubuntu 24.04).
// The probe was generated from the same list of cases.
const PrintfCase printfCases[] = {
    {"%d", {I(T::Int, 0LL)}, "0"sv, "0"sv},
    {"%d", {I(T::Int, 42LL)}, "42"sv, "42"sv},
    {"%d", {I(T::Int, -42LL)}, "-42"sv, "-42"sv},
    {"%d", {I(T::Int, -2147483648LL)}, "-2147483648"sv, "-2147483648"sv},
    {"%d", {I(T::Int, 2147483647LL)}, "2147483647"sv, "2147483647"sv},
    {"%i", {I(T::Int, -7LL)}, "-7"sv, "-7"sv},
    {"%u", {I(T::UnsignedInt, 4294967295LL)}, "4294967295"sv, "4294967295"sv},
    {"%u", {I(T::Int, -1LL)}, "4294967295"sv, "4294967295"sv},
    {"%o", {I(T::UnsignedInt, 8LL)}, "10"sv, "10"sv},
    {"%o", {I(T::UnsignedInt, 0LL)}, "0"sv, "0"sv},
    {"%x", {I(T::UnsignedInt, 255LL)}, "ff"sv, "ff"sv},
    {"%X", {I(T::UnsignedInt, 3735928559LL)}, "DEADBEEF"sv, "DEADBEEF"sv},
    {"%x", {I(T::Int, -1LL)}, "ffffffff"sv, "ffffffff"sv},
    {"%5d", {I(T::Int, 42LL)}, "   42"sv, "   42"sv},
    {"%-5d|", {I(T::Int, 42LL)}, "42   |"sv, "42   |"sv},
    {"%05d", {I(T::Int, 42LL)}, "00042"sv, "00042"sv},
    {"%05d", {I(T::Int, -42LL)}, "-0042"sv, "-0042"sv},
    {"%+d", {I(T::Int, 42LL)}, "+42"sv, "+42"sv},
    {"%+d", {I(T::Int, 0LL)}, "+0"sv, "+0"sv},
    {"% d", {I(T::Int, 42LL)}, " 42"sv, " 42"sv},
    {"% d", {I(T::Int, -42LL)}, "-42"sv, "-42"sv},
    {"%+ d", {I(T::Int, 42LL)}, "+42"sv, "+42"sv},
    {"%-05d|", {I(T::Int, -3LL)}, "-3   |"sv, "-3   |"sv},
    {"%.3d", {I(T::Int, 7LL)}, "007"sv, "007"sv},
    {"%.3d", {I(T::Int, -7LL)}, "-007"sv, "-007"sv},
    {"%.0d", {I(T::Int, 0LL)}, ""sv, ""sv},
    {"%5.0d|", {I(T::Int, 0LL)}, "     |"sv, "     |"sv},
    {"%.0u", {I(T::UnsignedInt, 0LL)}, ""sv, ""sv},
    {"%8.3d", {I(T::Int, -12LL)}, "    -012"sv, "    -012"sv},
    {"%08.3d", {I(T::Int, 12LL)}, "     012"sv, "     012"sv},
    {"%-8.3d|", {I(T::Int, 12LL)}, "012     |"sv, "012     |"sv},
    {"%#o", {I(T::UnsignedInt, 8LL)}, "010"sv, "010"sv},
    {"%#o", {I(T::UnsignedInt, 0LL)}, "0"sv, "0"sv},
    {"%#.0o", {I(T::UnsignedInt, 0LL)}, "0"sv, "0"sv},
    {"%#.5o", {I(T::UnsignedInt, 8LL)}, "00010"sv, "00010"sv},
    {"%#x", {I(T::UnsignedInt, 255LL)}, "0xff"sv, "0xff"sv},
    {"%#x", {I(T::UnsignedInt, 0LL)}, "0"sv, "0"sv},
    {"%#X", {I(T::UnsignedInt, 255LL)}, "0XFF"sv, "0XFF"sv},
    {"%#08x", {I(T::UnsignedInt, 255LL)}, "0x0000ff"sv, "0x0000ff"sv},
    {"%#.4x", {I(T::UnsignedInt, 15LL)}, "0x000f"sv, "0x000f"sv},
    {"%'d", {I(T::Int, 1234567LL)}, "1234567"sv, "1234567"sv},
    {"%20d|", {I(T::Int, -2147483648LL)}, "         -2147483648|"sv, "         -2147483648|"sv},
    {"%-20u|", {I(T::UnsignedInt, 123LL)}, "123                 |"sv, "123                 |"sv},
    {"%+.0d|", {I(T::Int, 0LL)}, "+|"sv, "+|"sv},
    {"%hhd", {I(T::Int, 300LL)}, "44"sv, "44"sv},
    {"%hhd", {I(T::Int, -129LL)}, "127"sv, "127"sv},
    {"%hhu", {I(T::Int, -1LL)}, "255"sv, "255"sv},
    {"%hhx", {I(T::Int, 511LL)}, "ff"sv, "ff"sv},
    {"%hd", {I(T::Int, 70000LL)}, "4464"sv, "4464"sv},
    {"%hd", {I(T::Int, -32769LL)}, "32767"sv, "32767"sv},
    {"%hu", {I(T::Int, -1LL)}, "65535"sv, "65535"sv},
    {"%hx", {I(T::Int, 74565LL)}, "2345"sv, "2345"sv},
    {"%ld", {I(T::Long, -1LL)}, "-1"sv, "-1"sv},
    {"%ld", {I(T::Long, -2147483648LL)}, "-2147483648"sv, "-2147483648"sv},
    {"%ld", {I(T::Long, (-9223372036854775807LL - 1))}, "-9223372036854775808"sv, "0"sv},
    {"%ld", {I(T::Long, 9223372036854775807LL)}, "9223372036854775807"sv, "-1"sv},
    {"%lu", {I(T::UnsignedLong, 18446744073709551615ULL)}, "18446744073709551615"sv, "4294967295"sv},
    {"%lx", {I(T::Long, -1LL)}, "ffffffffffffffff"sv, "ffffffff"sv},
    {"%lo", {I(T::UnsignedLong, 511LL)}, "777"sv, "777"sv},
    {"%li", {I(T::Long, 123456789012LL)}, "123456789012"sv, "-1097262572"sv},
    {"%lld", {I(T::LongLong, (-9223372036854775807LL - 1))}, "-9223372036854775808"sv, "-9223372036854775808"sv},
    {"%lld", {I(T::LongLong, 9223372036854775807LL)}, "9223372036854775807"sv, "9223372036854775807"sv},
    {"%llu", {I(T::UnsignedLongLong, 18446744073709551615ULL)}, "18446744073709551615"sv, "18446744073709551615"sv},
    {"%llx", {I(T::LongLong, -2LL)}, "fffffffffffffffe"sv, "fffffffffffffffe"sv},
    {"%llX", {I(T::UnsignedLongLong, 12379813738877118345ULL)}, "ABCDEF0123456789"sv, "ABCDEF0123456789"sv},
    {"%llo", {I(T::UnsignedLongLong, 18446744073709551615ULL)}, "1777777777777777777777"sv, "1777777777777777777777"sv},
    {"%qd", {I(T::LongLong, -5LL)}, "-5"sv, "-5"sv},
    {"%Ld", {I(T::LongLong, -5LL)}, "-5"sv, "-5"sv},
    {"%jd", {I(T::LongLong, (-9223372036854775807LL - 1))}, "-9223372036854775808"sv, "-9223372036854775808"sv},
    {"%ju", {I(T::UnsignedLongLong, 18446744073709551615ULL)}, "18446744073709551615"sv, "18446744073709551615"sv},
    {"%zu", {Z(18446744073709551615ULL)}, "18446744073709551615"sv, "4294967295"sv},
    {"%zd", {SZ(-1LL)}, "-1"sv, "-1"sv},
    {"%zx", {Z(4096LL)}, "1000"sv, "1000"sv},
    {"%Zu", {Z(7LL)}, "7"sv, "7"sv},
    {"%td", {PD(-100LL)}, "-100"sv, "-100"sv},
    {"%tu", {PD(-1LL)}, "18446744073709551615"sv, "4294967295"sv},
    {"%tx", {PD(255LL)}, "ff"sv, "ff"sv},
    {"%*d|", {I(T::Int, 6LL), I(T::Int, 1LL)}, "     1|"sv, "     1|"sv},
    {"%*d|", {I(T::Int, -6LL), I(T::Int, 2LL)}, "2     |"sv, "2     |"sv},
    {"%-*d|", {I(T::Int, -6LL), I(T::Int, 3LL)}, "3     |"sv, "3     |"sv},
    {"%.*d", {I(T::Int, 3LL), I(T::Int, 4LL)}, "004"sv, "004"sv},
    {"%.*d|", {I(T::Int, -3LL), I(T::Int, 5LL)}, "5|"sv, "5|"sv},
    {"%*.*d|", {I(T::Int, 8LL), I(T::Int, 4LL), I(T::Int, -6LL)}, "   -0006|"sv, "   -0006|"sv},
    {"%0*d", {I(T::Int, 5LL), I(T::Int, -9LL)}, "-0009"sv, "-0009"sv},
    {"%*s|", {I(T::Int, -4LL), S("ab")}, "ab  |"sv, "ab  |"sv},
    {"%.*s|", {I(T::Int, 2LL), S("abcdef")}, "ab|"sv, "ab|"sv},
    {"%*.*f|", {I(T::Int, 10LL), I(T::Int, 2LL), D(3.14159)}, "      3.14|"sv, "      3.14|"sv},
    {"%.*f", {I(T::Int, -1LL), D(2.5)}, "2.500000"sv, "2.500000"sv},
    {"%c", {I(T::Int, 65LL)}, "A"sv, "A"sv},
    {"%c", {I(T::Int, 321LL)}, "A"sv, "A"sv},
    {"%c", {I(T::Int, 200LL)}, "\310"sv, "\310"sv},
    {"%5c|", {I(T::Int, 120LL)}, "    x|"sv, "    x|"sv},
    {"%-5c|", {I(T::Int, 120LL)}, "x    |"sv, "x    |"sv},
    {"[%c]", {I(T::Int, 0LL)}, "[\000]"sv, "[\000]"sv},
    {"%s", {S("hello")}, "hello"sv, "hello"sv},
    {"%s", {S("")}, ""sv, ""sv},
    {"%10s|", {S("right")}, "     right|"sv, "     right|"sv},
    {"%-10s|", {S("left")}, "left      |"sv, "left      |"sv},
    {"%.3s|", {S("abcdef")}, "abc|"sv, "abc|"sv},
    {"%.0s|", {S("abc")}, "|"sv, "|"sv},
    {"%10.2s|", {S("abc")}, "        ab|"sv, "        ab|"sv},
    {"%s", {P(0)}, "(null)"sv, "(null)"sv},
    {"%.3s|", {P(0)}, "|"sv, "|"sv},
    {"%.6s|", {P(0)}, "(null)|"sv, "(null)|"sv},
    {"%10s|", {P(0)}, "    (null)|"sv, "    (null)|"sv},
    {"%-8s|", {P(0)}, "(null)  |"sv, "(null)  |"sv},
    {"%s and %s", {S("one"), S("two")}, "one and two"sv, "one and two"sv},
    {"%s", {S("tab\tnewline\n")}, "tab\tnewline\n"sv, "tab\tnewline\n"sv},
    {"%s", {S("\xc3\xa4")}, "\303\244"sv, "\303\244"sv},
    {"%p", {P(0x0)}, "(nil)"sv, "(nil)"sv},
    {"%p", {P(0x1234)}, "0x1234"sv, "0x1234"sv},
    {"%10p|", {P(0x0)}, "     (nil)|"sv, "     (nil)|"sv},
    {"%-10p|", {P(0x10)}, "0x10      |"sv, "0x10      |"sv},
    {"%20p|", {P(0xc0000000)}, "          0xc0000000|"sv, "          0xc0000000|"sv},
    {"%p", {P(0xffffffff)}, "0xffffffff"sv, "0xffffffff"sv},
    {"%p", {P(0x50000000000)}, "0x50000000000"sv, "(nil)"sv},
    {"100%%", {}, "100%"sv, "100%"sv},
    {"%%%d%%", {I(T::Int, 5LL)}, "%5%"sv, "%5%"sv},
    {"%5%|", {}, "%|"sv, "%|"sv},
    {"%-5%|", {}, "%|"sv, "%|"sv},
    {"%f", {D(0.0)}, "0.000000"sv, "0.000000"sv},
    {"%f", {D(-0.0)}, "-0.000000"sv, "-0.000000"sv},
    {"%f", {D(1.5)}, "1.500000"sv, "1.500000"sv},
    {"%f", {D(3.14159)}, "3.141590"sv, "3.141590"sv},
    {"%f",
     {D(1e300)},
     "1000000000000000052504760255204420248704468581108159154915854115511802457988908195786371375080447864043704443832883878176942523235360430575644792184786706982848387200926575803737830233794788090059368953234970799945081119038967640880074652742780142494579258788820056842838115669472196386865459400540160.000000"sv,
     "1000000000000000052504760255204420248704468581108159154915854115511802457988908195786371375080447864043704443832883878176942523235360430575644792184786706982848387200926575803737830233794788090059368953234970799945081119038967640880074652742780142494579258788820056842838115669472196386865459400540160.000000"sv},
    {"%f", {D(1e-300)}, "0.000000"sv, "0.000000"sv},
    {"%f", {D(123456789.0)}, "123456789.000000"sv, "123456789.000000"sv},
    {"%.20f", {D(0.1)}, "0.10000000000000000555"sv, "0.10000000000000000555"sv},
    {"%.0f", {D(0.5)}, "0"sv, "0"sv},
    {"%.0f", {D(1.5)}, "2"sv, "2"sv},
    {"%.0f", {D(2.5)}, "2"sv, "2"sv},
    {"%#.0f", {D(2.0)}, "2."sv, "2."sv},
    {"%.3f", {D(2.0005)}, "2.001"sv, "2.001"sv},
    {"%10.3f|", {D(-3.14159)}, "    -3.142|"sv, "    -3.142|"sv},
    {"%-10.3f|", {D(3.14159)}, "3.142     |"sv, "3.142     |"sv},
    {"%010.3f", {D(-3.14159)}, "-00003.142"sv, "-00003.142"sv},
    {"%+f", {D(1.0)}, "+1.000000"sv, "+1.000000"sv},
    {"% f", {D(1.0)}, " 1.000000"sv, " 1.000000"sv},
    {"%F", {D(inf)}, "INF"sv, "INF"sv},
    {"%f", {D(inf)}, "inf"sv, "inf"sv},
    {"%f", {D(-inf)}, "-inf"sv, "-inf"sv},
    {"%f", {D(nan)}, "nan"sv, "nan"sv},
    {"%f", {D(-nan)}, "-nan"sv, "-nan"sv},
    {"%F", {D(nan)}, "NAN"sv, "NAN"sv},
    {"%5f|", {D(nan)}, "  nan|"sv, "  nan|"sv},
    {"%-8f|", {D(-inf)}, "-inf    |"sv, "-inf    |"sv},
    {"%08f", {D(inf)}, "     inf"sv, "     inf"sv},
    {"%f",
     {D(DBL_MAX)},
     "179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.000000"sv,
     "179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.000000"sv},
    {"%.60f",
     {D(DBL_MIN)},
     "0.000000000000000000000000000000000000000000000000000000000000"sv,
     "0.000000000000000000000000000000000000000000000000000000000000"sv},
    {"%'.2f", {D(1234567.891)}, "1234567.89"sv, "1234567.89"sv},
    {"%lf", {D(2.25)}, "2.250000"sv, "2.250000"sv},
    {"%e", {D(0.0)}, "0.000000e+00"sv, "0.000000e+00"sv},
    {"%e", {D(1.5)}, "1.500000e+00"sv, "1.500000e+00"sv},
    {"%e", {D(-123456.789)}, "-1.234568e+05"sv, "-1.234568e+05"sv},
    {"%E", {D(1e-300)}, "1.000000E-300"sv, "1.000000E-300"sv},
    {"%.0e", {D(12345.0)}, "1e+04"sv, "1e+04"sv},
    {"%#.0e", {D(12345.0)}, "1.e+04"sv, "1.e+04"sv},
    {"%.10e", {D(3.14159)}, "3.1415900000e+00"sv, "3.1415900000e+00"sv},
    {"%+e", {D(1e300)}, "+1.000000e+300"sv, "+1.000000e+300"sv},
    {"%e", {D(DBL_MAX)}, "1.797693e+308"sv, "1.797693e+308"sv},
    {"%e", {D(4.9406564584124654e-324)}, "4.940656e-324"sv, "4.940656e-324"sv},
    {"%12.3e|", {D(0.000123)}, "   1.230e-04|"sv, "   1.230e-04|"sv},
    {"%e", {D(inf)}, "inf"sv, "inf"sv},
    {"%E", {D(nan)}, "NAN"sv, "NAN"sv},
    {"%g", {D(0.0)}, "0"sv, "0"sv},
    {"%g", {D(100000.0)}, "100000"sv, "100000"sv},
    {"%g", {D(1000000.0)}, "1e+06"sv, "1e+06"sv},
    {"%g", {D(0.0001)}, "0.0001"sv, "0.0001"sv},
    {"%g", {D(0.00001)}, "1e-05"sv, "1e-05"sv},
    {"%g", {D(1.5)}, "1.5"sv, "1.5"sv},
    {"%g", {D(3.14159265358979)}, "3.14159"sv, "3.14159"sv},
    {"%.10g", {D(3.14159265358979)}, "3.141592654"sv, "3.141592654"sv},
    {"%#g", {D(1.0)}, "1.00000"sv, "1.00000"sv},
    {"%#g", {D(100000.0)}, "100000."sv, "100000."sv},
    {"%.0g", {D(123.0)}, "1e+02"sv, "1e+02"sv},
    {"%.1g", {D(0.0001234)}, "0.0001"sv, "0.0001"sv},
    {"%G", {D(1e-10)}, "1E-10"sv, "1E-10"sv},
    {"%g", {D(1e300)}, "1e+300"sv, "1e+300"sv},
    {"%g", {D(DBL_MIN)}, "2.22507e-308"sv, "2.22507e-308"sv},
    {"%10.4g|", {D(-1.0 / 3.0)}, "   -0.3333|"sv, "   -0.3333|"sv},
    {"%-10g|", {D(2.5)}, "2.5       |"sv, "2.5       |"sv},
    {"%g", {D(inf)}, "inf"sv, "inf"sv},
    {"%G", {D(-inf)}, "-INF"sv, "-INF"sv},
    {"%g", {D(123456789.0)}, "1.23457e+08"sv, "1.23457e+08"sv},
    {"%a", {D(0.0)}, "0x0p+0"sv, "0x0p+0"sv},
    {"%a", {D(1.0)}, "0x1p+0"sv, "0x1p+0"sv},
    {"%a", {D(0.1)}, "0x1.999999999999ap-4"sv, "0x1.999999999999ap-4"sv},
    {"%A", {D(-0.1)}, "-0X1.999999999999AP-4"sv, "-0X1.999999999999AP-4"sv},
    {"%a", {D(DBL_MAX)}, "0x1.fffffffffffffp+1023"sv, "0x1.fffffffffffffp+1023"sv},
    {"%a", {D(DBL_MIN)}, "0x1p-1022"sv, "0x1p-1022"sv},
    {"%a", {D(4.9406564584124654e-324)}, "0x0.0000000000001p-1022"sv, "0x0.0000000000001p-1022"sv},
    {"%.3a", {D(1.0 / 3.0)}, "0x1.555p-2"sv, "0x1.555p-2"sv},
    {"%.0a", {D(1.5)}, "0x2p+0"sv, "0x2p+0"sv},
    {"%#a", {D(1.0)}, "0x1.p+0"sv, "0x1.p+0"sv},
    {"%20a|", {D(1.0)}, "              0x1p+0|"sv, "              0x1p+0|"sv},
    {"%a", {D(nan)}, "nan"sv, "nan"sv},
    {"%A", {D(inf)}, "INF"sv, "INF"sv},
    {"%Lf", {LD(1.1L)}, "1.100000"sv, "1.100000"sv},
    {"%Lf", {LD(-0.0L)}, "-0.000000"sv, "-0.000000"sv},
    {"%.20Lf", {LD(0.1L)}, "0.10000000000000000000"sv, "0.10000000000000000000"sv},
    {"%Le", {LD(LDBL_MAX)}, "1.189731e+4932"sv, "1.189731e+4932"sv},
    {"%Le", {LD(LDBL_MIN)}, "3.362103e-4932"sv, "3.362103e-4932"sv},
    {"%Lg", {LD(3.14159265358979323846264338327950288L)}, "3.14159"sv, "3.14159"sv},
    {"%.18Lg", {LD(3.14159265358979323846264338327950288L)}, "3.14159265358979324"sv, "3.14159265358979324"sv},
    {"%La", {LD(1.0L)}, "0x8p-3"sv, "0x8p-3"sv},
    {"%La", {LD(0.1L)}, "0xc.ccccccccccccccdp-7"sv, "0xc.ccccccccccccccdp-7"sv},
    {"%LA", {LD(LDBL_MAX)}, "0XF.FFFFFFFFFFFFFFFP+16380"sv, "0XF.FFFFFFFFFFFFFFFP+16380"sv},
    {"%La", {LD(0x1p-16445L)}, "0x0.000000000000001p-16385"sv, "0x0.000000000000001p-16385"sv},
    {"%Lf", {LD(std::numeric_limits<long double>::infinity())}, "inf"sv, "inf"sv},
    {"%Lf", {LD(std::numeric_limits<long double>::quiet_NaN())}, "nan"sv, "nan"sv},
    {"%10.2Lf|", {LD(-2.5L)}, "     -2.50|"sv, "     -2.50|"sv},
    {"%llf", {LD(1.5L)}, "1.500000"sv, "1.500000"sv},
    {"%d %s %c %f %x",
     {I(T::Int, -1LL), S("str"), I(T::Int, 66LL), D(0.5), I(T::UnsignedInt, 3054LL)},
     "-1 str B 0.500000 bee"sv,
     "-1 str B 0.500000 bee"sv},
    {"[%5d][%-5s][%5.1f]", {I(T::Int, 1LL), S("ab"), D(2.25)}, "[    1][ab   ][  2.2]"sv, "[    1][ab   ][  2.2]"sv},
    {"a%%b%dc%%", {I(T::Int, 9LL)}, "a%b9c%"sv, "a%b9c%"sv},
    {"%ld %lu %zu", {I(T::Long, -2LL), I(T::UnsignedLong, 3LL), Z(4LL)}, "-2 3 4"sv, "-2 3 4"sv},
    {"%hhd %hd %d %ld %lld",
     {I(T::Int, -1LL), I(T::Int, -2LL), I(T::Int, -3LL), I(T::Long, -4LL), I(T::LongLong, -5LL)},
     "-1 -2 -3 -4 -5"sv,
     "-1 -2 -3 -4 -5"sv},
    {"%s=%d, %s=%.2f", {S("x"), I(T::Int, 10LL), S("y"), D(0.125)}, "x=10, y=0.12"sv, "x=10, y=0.12"sv},
    {"no conversions", {}, "no conversions"sv, "no conversions"sv},
    {"", {}, ""sv, ""sv},
};

class PrintfTest : public LibraryTest {
protected:
    value::Value make(const Argument& argument) {
        const TargetInfo& target = context.target();
        switch (argument.kind) {
        case Argument::Kind::Integer:
        case Argument::Kind::Pointer:
            return integer(argument.integer, argument.type);
        case Argument::Kind::Size:
            return integer(argument.integer, target.sizeType());
        case Argument::Kind::SignedSize:
        case Argument::Kind::PtrDiff:
            return integer(argument.integer, target.ptrdiffType());
        case Argument::Kind::Double:
            return value::Value::ofDouble(argument.floating);
        case Argument::Kind::LongDouble:
            return value::Value::ofLongDouble(argument.longDouble);
        case Argument::Kind::String:
            return pointer(string(argument.text, Region::ReadOnly));
        }
        return {};
    }

    /// Calls `function` with the format as a string literal and the given arguments in front of it.
    LibraryResult print(std::string_view function, std::vector<value::Value> leading, std::string_view format,
                        std::vector<value::Value> arguments = {}) {
        leading.push_back(pointer(string(format, Region::ReadOnly)));
        leading.insert(leading.end(), arguments.begin(), arguments.end());
        return call(function, leading);
    }

    /// The value of the library object `stdout` or `stderr`.
    value::Value stream(std::string_view name) {
        return pointer(static_cast<Address>(readInteger(context.objectAddress(name), ScalarType::Pointer)));
    }
};

INSTANTIATE_TEST_SUITE_P(DataModels, PrintfTest, testing::Values(DataModel::ILP32, DataModel::LP64), modelName);

TEST_P(PrintfTest, FormatsLikeGlibc) {
    constexpr std::uint64_t bufferSize = 4096;
    for (const PrintfCase& printfCase : printfCases) {
        SCOPED_TRACE(printfCase.format);
        const std::string_view expected = lp64() ? printfCase.expected64 : printfCase.expected32;
        std::vector<value::Value> arguments;
        for (const Argument& argument : printfCase.arguments) {
            arguments.push_back(make(argument));
        }
        const Address output = buffer(bufferSize);
        const LibraryResult result =
            print("snprintf", {pointer(output), size(bufferSize)}, printfCase.format, arguments);
        EXPECT_EQ(returned(result), static_cast<Int128>(expected.size()));
        EXPECT_EQ(readText(output, expected.size() + 1), std::string(expected) + '\0');
    }
}

// ---- The functions of the family ----

TEST_P(PrintfTest, PrintfWritesToStdoutAndReturnsTheLength) {
    EXPECT_EQ(returned(print("printf", {}, "x=%d\n", {intValue(5)})), 4);
    EXPECT_EQ(returned(print("vprintf", {}, "%s!", {pointer(string("va"))})), 3);
    EXPECT_EQ(context.printed(), "x=5\nva!");
}

TEST_P(PrintfTest, FprintfWritesToStdoutAndDiscardsStderr) {
    EXPECT_EQ(returned(print("fprintf", {stream("stdout")}, "out %d", {intValue(1)})), 5);
    EXPECT_EQ(returned(print("fprintf", {stream("stderr")}, "err %d", {intValue(2)})), 5);
    EXPECT_EQ(returned(print("vfprintf", {stream("stdout")}, "!")), 1);
    EXPECT_EQ(context.printed(), "out 1!");
}

TEST_P(PrintfTest, FprintfToOtherStreams) {
    EXPECT_TRUE(isUndefined(print("fprintf", {pointer(0)}, "x"), "null stream"));
    EXPECT_TRUE(endedWith(print("fprintf", {stream("stdin")}, "x"), LibraryResult::Kind::Unsupported));
    EXPECT_TRUE(endedWith(print("fprintf", {pointer(buffer(64))}, "x"), LibraryResult::Kind::Unsupported));
}

TEST_P(PrintfTest, SprintfWritesATerminatedString) {
    const Address output = buffer(16);
    EXPECT_EQ(returned(print("sprintf", {pointer(output)}, "%d-%s", {intValue(-12), pointer(string("ab"))})), 6);
    EXPECT_EQ(readText(output, 7), "-12-ab"s + '\0');
    EXPECT_EQ(returned(print("vsprintf", {pointer(output)}, "")), 0);
    EXPECT_EQ(readText(output, 1), "\0"s);
}

TEST_P(PrintfTest, SprintfPastTheEndOfTheBufferIsUndefined) {
    const Address output = buffer(5);
    EXPECT_TRUE(isUndefined(print("sprintf", {pointer(output)}, "%d", {intValue(12345)}), "past the end"));
    EXPECT_TRUE(isUndefined(print("sprintf", {pointer(string("literal", Region::ReadOnly))}, "x"), "read-only"));
}

TEST_P(PrintfTest, SnprintfTruncatesAndReturnsTheFullLength) {
    const Address output = buffer(8);
    EXPECT_EQ(returned(print("snprintf", {pointer(output), size(4)}, "%s", {pointer(string("abcdef"))})), 6);
    EXPECT_EQ(readText(output, 4), "abc"s + '\0');
    EXPECT_FALSE(memory().isInitialized(output + 4, 1));
    EXPECT_EQ(returned(print("vsnprintf", {pointer(output), size(1)}, "xyz")), 3);
    EXPECT_EQ(readText(output, 1), "\0"s);
}

TEST_P(PrintfTest, SnprintfWithSizeZeroWritesNothing) {
    EXPECT_EQ(returned(print("snprintf", {pointer(0), size(0)}, "%d", {intValue(123)})), 3);
}

TEST_P(PrintfTest, AsprintfAllocatesTheOutput) {
    const Address result = buffer(8);
    EXPECT_EQ(returned(print("asprintf", {pointer(result)}, "%d%%", {intValue(42)})), 3);
    const auto block = static_cast<Address>(readInteger(result, ScalarType::Pointer));
    EXPECT_EQ(memory().find(block)->region, Region::Heap);
    EXPECT_EQ(readString(block), "42%");
}

// ---- Errors in the call ----

TEST_P(PrintfTest, TooFewArgumentsAreUndefined) {
    EXPECT_TRUE(isUndefined(print("printf", {}, "%d %d", {intValue(1)}), "too few arguments"));
    EXPECT_TRUE(isUndefined(print("printf", {}, "%*d", {intValue(1)}), "too few arguments"));
}

TEST_P(PrintfTest, ArgumentsOfTheWrongKindAreUndefined) {
    EXPECT_TRUE(isUndefined(print("printf", {}, "%d", {value::Value::ofDouble(1)}), "%d expects an integer"));
    EXPECT_TRUE(isUndefined(print("printf", {}, "%f", {intValue(1)}), "%f expects a double"));
    EXPECT_TRUE(isUndefined(print("printf", {}, "%Lf", {value::Value::ofDouble(1)}), "expects a long double"));
    EXPECT_TRUE(isUndefined(print("printf", {}, "%s", {value::Value::ofDouble(1)}), "expects a pointer"));
}

TEST_P(PrintfTest, FloatArgumentsAreAcceptedAsPromoted) {
    EXPECT_EQ(returned(print("printf", {}, "%.1f", {value::Value::ofFloat(0.5f)})), 3);
    EXPECT_EQ(context.printed(), "0.5");
}

TEST_P(PrintfTest, InvalidSpecificationsAreUndefined) {
    EXPECT_TRUE(isUndefined(print("printf", {}, "%y", {intValue(1)}), "invalid conversion specification %y"));
    EXPECT_TRUE(isUndefined(print("printf", {}, "abc%"), "ends within a conversion specification"));
    EXPECT_TRUE(isUndefined(print("printf", {}, "%hf", {value::Value::ofDouble(1)}), "invalid"));
    EXPECT_TRUE(isUndefined(print("printf", {}, "%lp", {pointer(0)}), "invalid"));
}

TEST_P(PrintfTest, UnsupportedSpecifications) {
    const Address count = buffer(4);
    EXPECT_TRUE(endedWith(print("printf", {}, "%n", {pointer(count)}), LibraryResult::Kind::Unsupported, "%n"));
    EXPECT_TRUE(endedWith(print("printf", {}, "%1$d", {intValue(1)}), LibraryResult::Kind::Unsupported, "positional"));
    EXPECT_TRUE(endedWith(print("printf", {}, "%ls", {pointer(count)}), LibraryResult::Kind::Unsupported, "wide"));
    EXPECT_TRUE(endedWith(print("printf", {}, "%lc", {intValue(65)}), LibraryResult::Kind::Unsupported, "wide"));
    EXPECT_TRUE(endedWith(print("printf", {}, "%m"), LibraryResult::Kind::Unsupported));
}

TEST_P(PrintfTest, HugeFieldsAreUnsupported) {
    EXPECT_TRUE(endedWith(print("printf", {}, "%2000000d", {intValue(1)}), LibraryResult::Kind::Unsupported));
    EXPECT_TRUE(
        endedWith(print("printf", {}, "%*d", {intValue(-2000000), intValue(1)}), LibraryResult::Kind::Unsupported));
    EXPECT_TRUE(
        endedWith(print("printf", {}, "%.2000000f", {value::Value::ofDouble(1)}), LibraryResult::Kind::Unsupported));
}

TEST_P(PrintfTest, StringsAreReadFromMemory) {
    EXPECT_TRUE(isUndefined(print("printf", {}, "%s", {pointer(bytesObject("abc"))}), "past the end"));
    EXPECT_TRUE(isUndefined(print("printf", {}, "%s", {pointer(0x10)}), "null pointer"));
    // With a precision, the array need not be terminated (C17 7.21.6.1p8).
    EXPECT_EQ(returned(print("printf", {}, "%.3s|%.2s", {pointer(bytesObject("abc")), pointer(bytesObject("xyz"))})),
              6);
    EXPECT_EQ(context.printed(), "abc|xy");
}

TEST_P(PrintfTest, TheFormatIsReadFromMemory) {
    EXPECT_TRUE(isUndefined(call("printf", {pointer(0)}), "null pointer"));
    EXPECT_TRUE(isUndefined(call("printf", {pointer(bytesObject("%d"))}), "past the end"));
    EXPECT_TRUE(isUndefined(call("printf", {}), "too few arguments"));
}

// ---- Characters and strings ----

TEST_P(PrintfTest, PutsAppendsANewline) {
    EXPECT_EQ(callInteger("puts", {pointer(string("hello"))}), 6); // glibc returns the number of bytes written
    EXPECT_EQ(callInteger("puts", {pointer(string(""))}), 1);
    EXPECT_EQ(context.printed(), "hello\n\n");
    EXPECT_TRUE(isUndefined(call("puts", {pointer(0)}), "null pointer"));
}

TEST_P(PrintfTest, FputsReturnsOne) {
    EXPECT_EQ(callInteger("fputs", {pointer(string("out")), stream("stdout")}), 1);
    EXPECT_EQ(callInteger("fputs", {pointer(string("err")), stream("stderr")}), 1);
    EXPECT_EQ(callInteger("fputs_unlocked", {pointer(string("!")), stream("stdout")}), 1);
    EXPECT_EQ(context.printed(), "out!");
}

TEST_P(PrintfTest, PutcharReturnsTheCharacterAsUnsignedChar) {
    EXPECT_EQ(callInteger("putchar", {intValue(0x141)}), 0x41);
    EXPECT_EQ(callInteger("putchar", {intValue(-1)}), 255);
    EXPECT_EQ(callInteger("fputc", {intValue('b'), stream("stdout")}), 'b');
    EXPECT_EQ(callInteger("putc", {intValue('c'), stream("stdout")}), 'c');
    EXPECT_EQ(callInteger("putc", {intValue('d'), stream("stderr")}), 'd');
    EXPECT_EQ(context.printed(), "A\xff"s + "bc");
}

TEST_P(PrintfTest, FwriteWritesElements) {
    const Address data = bytesObject("abcdef");
    EXPECT_EQ(callInteger("fwrite", {pointer(data), size(2), size(3), stream("stdout")}), 3);
    EXPECT_EQ(callInteger("fwrite", {pointer(data), size(0), size(3), stream("stdout")}), 0);
    EXPECT_EQ(context.printed(), "abcdef");
    EXPECT_TRUE(isUndefined(call("fwrite", {pointer(data), size(7), size(1), stream("stdout")}), "past the end"));
    const std::uint64_t huge = lp64() ? ~std::uint64_t{0} : 0xFFFF'FFFF;
    EXPECT_TRUE(isUndefined(call("fwrite", {pointer(data), size(static_cast<Int128>(huge)), size(1), stream("stdout")}),
                            "past the end"));
}

TEST_P(PrintfTest, FflushSucceeds) {
    EXPECT_EQ(callInteger("fflush", {stream("stdout")}), 0);
    EXPECT_EQ(callInteger("fflush", {pointer(0)}), 0);
}

/// A context without an output stream.
class SilentContext : public TestContext {
public:
    using TestContext::TestContext;
    std::ostream* output() override { return nullptr; }
};

TEST_P(PrintfTest, WithoutAnOutputStreamTheOutputIsDiscarded) {
    SilentContext silent(GetParam());
    const Address text = silent.memory().allocate(Region::Global, 2, 1, "text").value();
    ASSERT_TRUE(silent.memory().fill(text, std::byte{'x'}, 1).has_value());
    const std::vector arguments{pointer(text)};
    const LibraryResult result =
        findLibraryFunction("puts")(silent, LibraryCall{.name = "puts", .arguments = arguments});
    EXPECT_EQ(result.kind, LibraryResult::Kind::Return);
}

// ---- The stream objects ----

TEST_P(PrintfTest, StreamsAreDistinctNonNullPointers) {
    const Int128 in = stream("stdin").signedBits();
    const Int128 out = stream("stdout").signedBits();
    const Int128 err = stream("stderr").signedBits();
    EXPECT_NE(in, 0);
    EXPECT_NE(out, 0);
    EXPECT_NE(err, 0);
    EXPECT_NE(in, out);
    EXPECT_NE(out, err);
    EXPECT_NE(in, err);
    EXPECT_EQ(memory().find(static_cast<Address>(out))->size, lp64() ? 216u : 148u); // sizeof(FILE)
}

} // namespace
} // namespace nitwit::interp::test
