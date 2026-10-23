#include <cerrno>
#include <cfenv>
#include <climits>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "library_test_support.hpp"

namespace nitwit::interp::test {
namespace {

// Infinity and NaN in each format (INFINITY and NAN are floats, whose implicit conversion -Wdouble-promotion rejects).
constexpr long double inf = std::numeric_limits<long double>::infinity();
constexpr long double nan = std::numeric_limits<long double>::quiet_NaN();
constexpr double doubleInf = std::numeric_limits<double>::infinity();
constexpr double doubleNan = std::numeric_limits<double>::quiet_NaN();
constexpr float floatInf = std::numeric_limits<float>::infinity();
constexpr float floatNan = std::numeric_limits<float>::quiet_NaN();

/// An argument of a math case; OUT is the pointer argument of frexp, modf and remquo.
struct Argument {
    enum class Kind : std::uint8_t { Float, Double, LongDouble, Int, Long, Out };
    Kind kind = Kind::Double;
    long double number = 0;
};

// The probe converted each double argument to the parameter type, as these do.
template <typename T>
Argument F(T value) {
    return {.kind = Argument::Kind::Float, .number = static_cast<long double>(static_cast<float>(value))};
}
template <typename T>
Argument D(T value) {
    return {.kind = Argument::Kind::Double, .number = static_cast<long double>(static_cast<double>(value))};
}
template <typename T>
Argument LD(T value) {
    return {.kind = Argument::Kind::LongDouble, .number = static_cast<long double>(static_cast<double>(value))};
}
Argument I(int value) {
    return {.kind = Argument::Kind::Int, .number = static_cast<long double>(value)};
}
Argument L(long value) {
    return {.kind = Argument::Kind::Long, .number = static_cast<long double>(value)};
}
const Argument OUT{.kind = Argument::Kind::Out, .number = 0};

/// The result (converted to long double, exactly), errno, and what the function stored through its pointer argument:
/// frexp's exponent, modf's integral part, remquo's quotient.
struct Outcome {
    long double result = 0;
    int error = 0;
    long double stored = 0;
};

struct MathCase {
    const char* function;
    std::vector<Argument> arguments;
    Outcome expected64;
    Outcome expected32;
};

// The results printed (with %La) by a program compiled with gcc-14 -O0 -fno-builtin, -m64 and -m32 -msse2
// -mfpmath=sse, linked to glibc 2.39 (Ubuntu 24.04). The probe was generated from the same list. The interpreter
// computes with the host's x86-64 libm in both data models: only the conversions to long depend on the data model.
// The comments note where i386's libm differs from x86-64's.
const MathCase mathCases[] = {
    {"acos", {D(0.5)}, {0x8.60a91c16b9b3p-3L, 0, 0x0p+0L}, {0x8.60a91c16b9b3p-3L, 0, 0x0p+0L}},
    {"acos", {D(2.0)}, {nan, 33, 0x0p+0L}, {nan, 33, 0x0p+0L}},
    {"acosf", {F(0.5)}, {0x8.60a92p-3L, 0, 0x0p+0L}, {0x8.60a92p-3L, 0, 0x0p+0L}},
    {"acosf", {F(2.0)}, {nan, 33, 0x0p+0L}, {nan, 33, 0x0p+0L}},
    {"acosl", {LD(0.5)}, {0x8.60a91c16b9b2c23p-3L, 0, 0x0p+0L}, {0x8.60a91c16b9b2c23p-3L, 0, 0x0p+0L}},
    {"acosl", {LD(2.0)}, {nan, 33, 0x0p+0L}, {nan, 33, 0x0p+0L}},
    {"asin", {D(0.5)}, {0x8.60a91c16b9b3p-4L, 0, 0x0p+0L}, {0x8.60a91c16b9b3p-4L, 0, 0x0p+0L}},
    {"asin", {D(-1.5)}, {nan, 33, 0x0p+0L}, {nan, 33, 0x0p+0L}},
    {"asinf", {F(0.5)}, {0x8.60a92p-4L, 0, 0x0p+0L}, {0x8.60a92p-4L, 0, 0x0p+0L}},
    {"asinf", {F(-1.5)}, {nan, 33, 0x0p+0L}, {nan, 33, 0x0p+0L}},
    {"asinl", {LD(0.5)}, {0x8.60a91c16b9b2c23p-4L, 0, 0x0p+0L}, {0x8.60a91c16b9b2c23p-4L, 0, 0x0p+0L}},
    {"asinl", {LD(-1.5)}, {nan, 33, 0x0p+0L}, {nan, 33, 0x0p+0L}},
    {"atan", {D(1.5)}, {0xf.b985e940fb4d8p-4L, 0, 0x0p+0L}, {0xf.b985e940fb4d8p-4L, 0, 0x0p+0L}},
    {"atanf", {F(1.5)}, {0xf.b985fp-4L, 0, 0x0p+0L}, {0xf.b985fp-4L, 0, 0x0p+0L}},
    {"atanl", {LD(1.5)}, {0xf.b985e940fb4d9p-4L, 0, 0x0p+0L}, {0xf.b985e940fb4d9p-4L, 0, 0x0p+0L}},
    {"atan2", {D(1.0), D(-1.0)}, {0x9.6cbe3f9990e9p-2L, 0, 0x0p+0L}, {0x9.6cbe3f9990e9p-2L, 0, 0x0p+0L}},
    {"atan2", {D(-0.0), D(-1.0)}, {-0xc.90fdaa22168cp-2L, 0, 0x0p+0L}, {-0xc.90fdaa22168cp-2L, 0, 0x0p+0L}},
    {"atan2f", {F(1.0), F(-1.0)}, {0x9.6cbe4p-2L, 0, 0x0p+0L}, {0x9.6cbe4p-2L, 0, 0x0p+0L}},
    {"atan2f", {F(-0.0), F(-1.0)}, {-0xc.90fdbp-2L, 0, 0x0p+0L}, {-0xc.90fdbp-2L, 0, 0x0p+0L}},
    {"atan2l", {LD(1.0), LD(-1.0)}, {0x9.6cbe3f9990e91a8p-2L, 0, 0x0p+0L}, {0x9.6cbe3f9990e91a8p-2L, 0, 0x0p+0L}},
    {"atan2l", {LD(-0.0), LD(-1.0)}, {-0xc.90fdaa22168c235p-2L, 0, 0x0p+0L}, {-0xc.90fdaa22168c235p-2L, 0, 0x0p+0L}},
    {"cos", {D(0.7)}, {0xc.3ccb294fcec98p-4L, 0, 0x0p+0L}, {0xc.3ccb294fcec98p-4L, 0, 0x0p+0L}},
    {"cos", {D(1e22)}, {0x8.5f167780e47ap-4L, 0, 0x0p+0L}, {0x8.5f167780e47ap-4L, 0, 0x0p+0L}},
    {"cosf", {F(0.7)}, {0xc.3ccb3p-4L, 0, 0x0p+0L}, {0xc.3ccb3p-4L, 0, 0x0p+0L}},
    {"cosf", {F(1e22)}, {0xa.dd6f7p-4L, 0, 0x0p+0L}, {0xa.dd6f7p-4L, 0, 0x0p+0L}},
    {"cosl", {LD(0.7)}, {0xc.3ccb294fcec951cp-4L, 0, 0x0p+0L}, {0xc.3ccb294fcec951cp-4L, 0, 0x0p+0L}},
    {"cosl", {LD(1e22)}, {0x8.5f167780e479c9ap-4L, 0, 0x0p+0L}, {0x8.5f167780e479c9ap-4L, 0, 0x0p+0L}},
    {"sin", {D(0.7)}, {0xa.4eb734a30cdcp-4L, 0, 0x0p+0L}, {0xa.4eb734a30cdcp-4L, 0, 0x0p+0L}},
    {"sin", {D(-3.0)}, {-0x9.081c36db6aad8p-6L, 0, 0x0p+0L}, {-0x9.081c36db6aad8p-6L, 0, 0x0p+0L}},
    {"sinf", {F(0.7)}, {0xa.4eb73p-4L, 0, 0x0p+0L}, {0xa.4eb73p-4L, 0, 0x0p+0L}},
    {"sinf", {F(-3.0)}, {-0x9.081c3p-6L, 0, 0x0p+0L}, {-0x9.081c3p-6L, 0, 0x0p+0L}},
    {"sinl", {LD(0.7)}, {0xa.4eb734a30cdc035p-4L, 0, 0x0p+0L}, {0xa.4eb734a30cdc035p-4L, 0, 0x0p+0L}},
    {"sinl", {LD(-3.0)}, {-0x9.081c36db6aada79p-6L, 0, 0x0p+0L}, {-0x9.081c36db6aada79p-6L, 0, 0x0p+0L}},
    {"tan", {D(0.7)}, {0xd.7a03617e3c57p-4L, 0, 0x0p+0L}, {0xd.7a03617e3c57p-4L, 0, 0x0p+0L}},
    {"tanf", {F(0.7)}, {0xd.7a036p-4L, 0, 0x0p+0L}, {0xd.7a036p-4L, 0, 0x0p+0L}},
    {"tanl", {LD(0.7)}, {0xd.7a03617e3c56d2ep-4L, 0, 0x0p+0L}, {0xd.7a03617e3c56d2ep-4L, 0, 0x0p+0L}},
    {"acosh", {D(2.5)}, {0xc.88ce09d18dd8p-3L, 0, 0x0p+0L}, {0xc.88ce09d18dd8p-3L, 0, 0x0p+0L}},
    {"acosh", {D(0.5)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"acoshf", {F(2.5)}, {0xc.88ce1p-3L, 0, 0x0p+0L}, {0xc.88ce1p-3L, 0, 0x0p+0L}},
    {"acoshf", {F(0.5)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"acoshl", {LD(2.5)}, {0xc.88ce09d18dd7ff3p-3L, 0, 0x0p+0L}, {0xc.88ce09d18dd7ff3p-3L, 0, 0x0p+0L}},
    {"acoshl", {LD(0.5)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"asinh",
     {D(1.5)},
     {0x9.8ee00485b1ec8p-3L, 0, 0x0p+0L},
     {0x9.8ee00485b1ec8p-3L, 0, 0x0p+0L}}, // native -m32: 0x9.8ee00485b1ecp-3
    {"asinhf", {F(1.5)}, {0x9.8eep-3L, 0, 0x0p+0L}, {0x9.8eep-3L, 0, 0x0p+0L}},
    {"asinhl", {LD(1.5)}, {0x9.8ee00485b1ec341p-3L, 0, 0x0p+0L}, {0x9.8ee00485b1ec341p-3L, 0, 0x0p+0L}},
    {"atanh",
     {D(0.5)},
     {0x8.c9f53d568185p-4L, 0, 0x0p+0L},
     {0x8.c9f53d568185p-4L, 0, 0x0p+0L}}, // native -m32: 0x8.c9f53d5681858p-4
    {"atanh", {D(1.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"atanhf", {F(0.5)}, {0x8.c9f54p-4L, 0, 0x0p+0L}, {0x8.c9f54p-4L, 0, 0x0p+0L}},
    {"atanhf", {F(1.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"atanhl", {LD(0.5)}, {0x8.c9f53d5681854bcp-4L, 0, 0x0p+0L}, {0x8.c9f53d5681854bcp-4L, 0, 0x0p+0L}},
    {"atanhl", {LD(1.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"cosh", {D(1.2)}, {0xe.7c38fc6fd4p-3L, 0, 0x0p+0L}, {0xe.7c38fc6fd4p-3L, 0, 0x0p+0L}},
    {"cosh", {D(1e5)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"coshf", {F(1.2)}, {0xe.7c39p-3L, 0, 0x0p+0L}, {0xe.7c39p-3L, 0, 0x0p+0L}},
    {"coshf", {F(1e5)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"coshl", {LD(1.2)}, {0xe.7c38fc6fd400065p-3L, 0, 0x0p+0L}, {0xe.7c38fc6fd400065p-3L, 0, 0x0p+0L}},
    {"coshl", {LD(1e5)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"sinh", {D(1.2)}, {0xc.136079a091a4p-3L, 0, 0x0p+0L}, {0xc.136079a091a4p-3L, 0, 0x0p+0L}},
    {"sinhf", {F(1.2)}, {0xc.13608p-3L, 0, 0x0p+0L}, {0xc.13608p-3L, 0, 0x0p+0L}},
    {"sinhl", {LD(1.2)}, {0xc.136079a091a435bp-3L, 0, 0x0p+0L}, {0xc.136079a091a435bp-3L, 0, 0x0p+0L}},
    {"tanh", {D(0.3)}, {0x9.526ed3ec8a7dp-5L, 0, 0x0p+0L}, {0x9.526ed3ec8a7dp-5L, 0, 0x0p+0L}},
    {"tanhf", {F(0.3)}, {0x9.526edp-5L, 0, 0x0p+0L}, {0x9.526edp-5L, 0, 0x0p+0L}}, // native -m32: 0x9.526eep-5
    {"tanhl", {LD(0.3)}, {0x9.526ed3ec8a7cf11p-5L, 0, 0x0p+0L}, {0x9.526ed3ec8a7cf11p-5L, 0, 0x0p+0L}},
    {"exp", {D(3.7)}, {0xa.1ca0a27776408p+2L, 0, 0x0p+0L}, {0xa.1ca0a27776408p+2L, 0, 0x0p+0L}},
    {"exp", {D(1000.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"exp", {D(-1000.0)}, {0x0p+0L, 34, 0x0p+0L}, {0x0p+0L, 34, 0x0p+0L}},
    {"expf", {F(3.7)}, {0xa.1ca0bp+2L, 0, 0x0p+0L}, {0xa.1ca0bp+2L, 0, 0x0p+0L}},
    {"expf", {F(1000.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"expf", {F(-1000.0)}, {0x0p+0L, 34, 0x0p+0L}, {0x0p+0L, 34, 0x0p+0L}},
    {"expl", {LD(3.7)}, {0xa.1ca0a27776406a1p+2L, 0, 0x0p+0L}, {0xa.1ca0a27776406a1p+2L, 0, 0x0p+0L}},
    {"expl", {LD(1000.0)}, {0xc.f391bcd76b9d6cp+1439L, 0, 0x0p+0L}, {0xc.f391bcd76b9d6cp+1439L, 0, 0x0p+0L}},
    {"expl", {LD(-1000.0)}, {0x9.e210cf20c4aa07ap-1446L, 0, 0x0p+0L}, {0x9.e210cf20c4aa07ap-1446L, 0, 0x0p+0L}},
    {"exp2", {D(10.5)}, {0xb.504f333f9de68p+7L, 0, 0x0p+0L}, {0xb.504f333f9de68p+7L, 0, 0x0p+0L}},
    {"exp2f", {F(10.5)}, {0xb.504f3p+7L, 0, 0x0p+0L}, {0xb.504f3p+7L, 0, 0x0p+0L}},
    {"exp2l", {LD(10.5)}, {0xb.504f333f9de6484p+7L, 0, 0x0p+0L}, {0xb.504f333f9de6484p+7L, 0, 0x0p+0L}},
    {"expm1", {D(1e-5)}, {0xa.7c5e340e6fdbp-20L, 0, 0x0p+0L}, {0xa.7c5e340e6fdbp-20L, 0, 0x0p+0L}},
    {"expm1f", {F(1e-5)}, {0xa.7c5e3p-20L, 0, 0x0p+0L}, {0xa.7c5e3p-20L, 0, 0x0p+0L}},
    {"expm1l", {LD(1e-5)}, {0xa.7c5e340e6fdae88p-20L, 0, 0x0p+0L}, {0xa.7c5e340e6fdae88p-20L, 0, 0x0p+0L}},
    {"frexp", {D(48.0), OUT}, {0xcp-4L, 0, 0xcp-1L}, {0xcp-4L, 0, 0xcp-1L}},
    {"frexp", {D(-0.0), OUT}, {-0x0p+0L, 0, 0x0p+0L}, {-0x0p+0L, 0, 0x0p+0L}},
    {"frexpf", {F(48.0), OUT}, {0xcp-4L, 0, 0xcp-1L}, {0xcp-4L, 0, 0xcp-1L}},
    {"frexpf", {F(-0.0), OUT}, {-0x0p+0L, 0, 0x0p+0L}, {-0x0p+0L, 0, 0x0p+0L}},
    {"frexpl", {LD(48.0), OUT}, {0xcp-4L, 0, 0xcp-1L}, {0xcp-4L, 0, 0xcp-1L}},
    {"frexpl", {LD(-0.0), OUT}, {-0x0p+0L, 0, 0x0p+0L}, {-0x0p+0L, 0, 0x0p+0L}},
    {"ilogb", {D(1000.0)}, {0x9p+0L, 0, 0x0p+0L}, {0x9p+0L, 0, 0x0p+0L}},
    {"ilogb", {D(0.0)}, {-0x8p+28L, 33, 0x0p+0L}, {-0x8p+28L, 33, 0x0p+0L}},
    {"ilogbf", {F(1000.0)}, {0x9p+0L, 0, 0x0p+0L}, {0x9p+0L, 0, 0x0p+0L}},
    {"ilogbf", {F(0.0)}, {-0x8p+28L, 33, 0x0p+0L}, {-0x8p+28L, 33, 0x0p+0L}},
    {"ilogbl", {LD(1000.0)}, {0x9p+0L, 0, 0x0p+0L}, {0x9p+0L, 0, 0x0p+0L}},
    {"ilogbl", {LD(0.0)}, {-0x8p+28L, 33, 0x0p+0L}, {-0x8p+28L, 33, 0x0p+0L}},
    {"ldexp", {D(0.75), I(4)}, {0xcp+0L, 0, 0x0p+0L}, {0xcp+0L, 0, 0x0p+0L}},
    {"ldexp", {D(1.0), I(50000)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"ldexpf", {F(0.75), I(4)}, {0xcp+0L, 0, 0x0p+0L}, {0xcp+0L, 0, 0x0p+0L}},
    {"ldexpf", {F(1.0), I(50000)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"ldexpl", {LD(0.75), I(4)}, {0xcp+0L, 0, 0x0p+0L}, {0xcp+0L, 0, 0x0p+0L}},
    {"ldexpl", {LD(1.0), I(50000)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"log", {D(3.7)}, {0xa.7777328572a8p-3L, 0, 0x0p+0L}, {0xa.7777328572a8p-3L, 0, 0x0p+0L}},
    {"log", {D(0.0)}, {-inf, 34, 0x0p+0L}, {-inf, 34, 0x0p+0L}},
    {"log", {D(-1.0)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"logf", {F(3.7)}, {0xa.77773p-3L, 0, 0x0p+0L}, {0xa.77773p-3L, 0, 0x0p+0L}},
    {"logf", {F(0.0)}, {-inf, 34, 0x0p+0L}, {-inf, 34, 0x0p+0L}},
    {"logf", {F(-1.0)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"logl", {LD(3.7)}, {0xa.7777328572a7d07p-3L, 0, 0x0p+0L}, {0xa.7777328572a7d07p-3L, 0, 0x0p+0L}},
    {"logl", {LD(0.0)}, {-inf, 34, 0x0p+0L}, {-inf, 34, 0x0p+0L}},
    {"logl", {LD(-1.0)}, {nan, 33, 0x0p+0L}, {nan, 33, 0x0p+0L}},
    {"log10", {D(1000.5)}, {0xc.0038e8dcdd1ap-2L, 0, 0x0p+0L}, {0xc.0038e8dcdd1ap-2L, 0, 0x0p+0L}},
    {"log10f", {F(1000.5)}, {0xc.0038fp-2L, 0, 0x0p+0L}, {0xc.0038fp-2L, 0, 0x0p+0L}},
    {"log10l", {LD(1000.5)}, {0xc.0038e8dcdd1a2e1p-2L, 0, 0x0p+0L}, {0xc.0038e8dcdd1a2e1p-2L, 0, 0x0p+0L}},
    {"log1p", {D(1e-5)}, {0xa.7c5754d7398b8p-20L, 0, 0x0p+0L}, {0xa.7c5754d7398b8p-20L, 0, 0x0p+0L}},
    {"log1p", {D(-1.0)}, {-inf, 34, 0x0p+0L}, {-inf, 34, 0x0p+0L}},
    {"log1pf", {F(1e-5)}, {0xa.7c575p-20L, 0, 0x0p+0L}, {0xa.7c575p-20L, 0, 0x0p+0L}},
    {"log1pf", {F(-1.0)}, {-inf, 34, 0x0p+0L}, {-inf, 34, 0x0p+0L}},
    {"log1pl", {LD(1e-5)}, {0xa.7c5754d7398b6ep-20L, 0, 0x0p+0L}, {0xa.7c5754d7398b6ep-20L, 0, 0x0p+0L}},
    {"log1pl", {LD(-1.0)}, {-inf, 34, 0x0p+0L}, {-inf, 34, 0x0p+0L}},
    {"log2", {D(10.0)}, {0xd.49a784bcd1b88p-2L, 0, 0x0p+0L}, {0xd.49a784bcd1b88p-2L, 0, 0x0p+0L}},
    {"log2f", {F(10.0)}, {0xd.49a78p-2L, 0, 0x0p+0L}, {0xd.49a78p-2L, 0, 0x0p+0L}},
    {"log2l", {LD(10.0)}, {0xd.49a784bcd1b8afep-2L, 0, 0x0p+0L}, {0xd.49a784bcd1b8afep-2L, 0, 0x0p+0L}},
    {"logb", {D(100.0)}, {0xcp-1L, 0, 0x0p+0L}, {0xcp-1L, 0, 0x0p+0L}},
    {"logbf", {F(100.0)}, {0xcp-1L, 0, 0x0p+0L}, {0xcp-1L, 0, 0x0p+0L}},
    {"logbl", {LD(100.0)}, {0xcp-1L, 0, 0x0p+0L}, {0xcp-1L, 0, 0x0p+0L}},
    {"modf", {D(-3.75), OUT}, {-0xcp-4L, 0, -0xcp-2L}, {-0xcp-4L, 0, -0xcp-2L}},
    {"modf", {D(inf), OUT}, {0x0p+0L, 0, inf}, {0x0p+0L, 0, inf}},
    {"modff", {F(-3.75), OUT}, {-0xcp-4L, 0, -0xcp-2L}, {-0xcp-4L, 0, -0xcp-2L}},
    {"modff", {F(inf), OUT}, {0x0p+0L, 0, inf}, {0x0p+0L, 0, inf}},
    {"modfl", {LD(-3.75), OUT}, {-0xcp-4L, 0, -0xcp-2L}, {-0xcp-4L, 0, -0xcp-2L}},
    {"modfl", {LD(inf), OUT}, {0x0p+0L, 0, inf}, {0x0p+0L, 0, inf}},
    {"scalbn", {D(3.0), I(-2)}, {0xcp-4L, 0, 0x0p+0L}, {0xcp-4L, 0, 0x0p+0L}},
    {"scalbnf", {F(3.0), I(-2)}, {0xcp-4L, 0, 0x0p+0L}, {0xcp-4L, 0, 0x0p+0L}},
    {"scalbnl", {LD(3.0), I(-2)}, {0xcp-4L, 0, 0x0p+0L}, {0xcp-4L, 0, 0x0p+0L}},
    {"scalbln", {D(3.0), L(10)}, {0xcp+8L, 0, 0x0p+0L}, {0xcp+8L, 0, 0x0p+0L}},
    {"scalblnf", {F(3.0), L(10)}, {0xcp+8L, 0, 0x0p+0L}, {0xcp+8L, 0, 0x0p+0L}},
    {"scalblnl", {LD(3.0), L(10)}, {0xcp+8L, 0, 0x0p+0L}, {0xcp+8L, 0, 0x0p+0L}},
    {"cbrt", {D(27.5)}, {0xc.12d8db5d601d8p-2L, 0, 0x0p+0L}, {0xc.12d8db5d601d8p-2L, 0, 0x0p+0L}},
    {"cbrtf", {F(27.5)}, {0xc.12d8ep-2L, 0, 0x0p+0L}, {0xc.12d8ep-2L, 0, 0x0p+0L}},
    {"cbrtl",
     {LD(27.5)},
     {0xc.12d8db5d601d72ap-2L, 0, 0x0p+0L},
     {0xc.12d8db5d601d72ap-2L, 0, 0x0p+0L}}, // native -m32: 0xc.12d8db5d601d72bp-2
    {"fabs", {D(-2.5)}, {0xap-2L, 0, 0x0p+0L}, {0xap-2L, 0, 0x0p+0L}},
    {"fabs", {D(-nan)}, {nan, 0, 0x0p+0L}, {nan, 0, 0x0p+0L}},
    {"fabsf", {F(-2.5)}, {0xap-2L, 0, 0x0p+0L}, {0xap-2L, 0, 0x0p+0L}},
    {"fabsf", {F(-nan)}, {nan, 0, 0x0p+0L}, {nan, 0, 0x0p+0L}},
    {"fabsl", {LD(-2.5)}, {0xap-2L, 0, 0x0p+0L}, {0xap-2L, 0, 0x0p+0L}},
    {"fabsl", {LD(-nan)}, {nan, 0, 0x0p+0L}, {nan, 0, 0x0p+0L}},
    {"hypot", {D(3.0), D(4.5)}, {0xa.d11039a481bb8p-1L, 0, 0x0p+0L}, {0xa.d11039a481bb8p-1L, 0, 0x0p+0L}},
    {"hypotf", {F(3.0), F(4.5)}, {0xa.d1104p-1L, 0, 0x0p+0L}, {0xa.d1104p-1L, 0, 0x0p+0L}},
    {"hypotl", {LD(3.0), LD(4.5)}, {0xa.d11039a481bb446p-1L, 0, 0x0p+0L}, {0xa.d11039a481bb446p-1L, 0, 0x0p+0L}},
    {"pow", {D(3.7), D(0.1)}, {0x9.1e43cb385146p-3L, 0, 0x0p+0L}, {0x9.1e43cb385146p-3L, 0, 0x0p+0L}},
    {"pow", {D(0.0), D(-1.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"pow", {D(-2.0), D(0.5)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"pow", {D(10.0), D(400.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"powf", {F(3.7), F(0.1)}, {0x9.1e43dp-3L, 0, 0x0p+0L}, {0x9.1e43dp-3L, 0, 0x0p+0L}},
    {"powf", {F(0.0), F(-1.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"powf", {F(-2.0), F(0.5)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"powf", {F(10.0), F(400.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"powl", {LD(3.7), LD(0.1)}, {0x9.1e43cb385145c2p-3L, 0, 0x0p+0L}, {0x9.1e43cb385145c2p-3L, 0, 0x0p+0L}},
    {"powl", {LD(0.0), LD(-1.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"powl", {LD(-2.0), LD(0.5)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"powl", {LD(10.0), LD(400.0)}, {0xd.a763fc8cb9ff9e6p+1325L, 0, 0x0p+0L}, {0xd.a763fc8cb9ff9e6p+1325L, 0, 0x0p+0L}},
    {"sqrt", {D(2.0)}, {0xb.504f333f9de68p-3L, 0, 0x0p+0L}, {0xb.504f333f9de68p-3L, 0, 0x0p+0L}},
    {"sqrt", {D(-1.0)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"sqrtf", {F(2.0)}, {0xb.504f3p-3L, 0, 0x0p+0L}, {0xb.504f3p-3L, 0, 0x0p+0L}},
    {"sqrtf", {F(-1.0)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"sqrtl", {LD(2.0)}, {0xb.504f333f9de6484p-3L, 0, 0x0p+0L}, {0xb.504f333f9de6484p-3L, 0, 0x0p+0L}},
    {"sqrtl", {LD(-1.0)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"erf", {D(0.1)}, {0xe.652f5127d99ap-7L, 0, 0x0p+0L}, {0xe.652f5127d99ap-7L, 0, 0x0p+0L}},
    {"erff", {F(0.1)}, {0xe.652f5p-7L, 0, 0x0p+0L}, {0xe.652f5p-7L, 0, 0x0p+0L}},
    {"erfl", {LD(0.1)}, {0xe.652f5127d99a088p-7L, 0, 0x0p+0L}, {0xe.652f5127d99a088p-7L, 0, 0x0p+0L}},
    {"erfc",
     {D(0.1)},
     {0xe.335a15db04cdp-4L, 0, 0x0p+0L},
     {0xe.335a15db04cdp-4L, 0, 0x0p+0L}}, // native -m32: 0xe.335a15db04cc8p-4
    {"erfcf", {F(0.1)}, {0xe.335a1p-4L, 0, 0x0p+0L}, {0xe.335a1p-4L, 0, 0x0p+0L}},
    {"erfcl", {LD(0.1)}, {0xe.335a15db04ccbefp-4L, 0, 0x0p+0L}, {0xe.335a15db04ccbefp-4L, 0, 0x0p+0L}},
    {"lgamma", {D(0.5)}, {0x9.28682473d0de8p-4L, 0, 0x0p+0L}, {0x9.28682473d0de8p-4L, 0, 0x0p+0L}},
    {"lgamma", {D(-1.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"lgammaf", {F(0.5)}, {0x9.28682p-4L, 0, 0x0p+0L}, {0x9.28682p-4L, 0, 0x0p+0L}},
    {"lgammaf", {F(-1.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"lgammal", {LD(0.5)}, {0x9.28682473d0de85fp-4L, 0, 0x0p+0L}, {0x9.28682473d0de85fp-4L, 0, 0x0p+0L}},
    {"lgammal", {LD(-1.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"tgamma", {D(4.5)}, {0xb.a1b8f3c33633p+0L, 0, 0x0p+0L}, {0xb.a1b8f3c33633p+0L, 0, 0x0p+0L}},
    {"tgamma", {D(0.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"tgammaf", {F(4.5)}, {0xb.a1b9p+0L, 0, 0x0p+0L}, {0xb.a1b9p+0L, 0, 0x0p+0L}}, // native -m32: 0xb.a1b8fp+0
    {"tgammaf", {F(0.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"tgammal", {LD(4.5)}, {0xb.a1b8f3c33632becp+0L, 0, 0x0p+0L}, {0xb.a1b8f3c33632becp+0L, 0, 0x0p+0L}},
    {"tgammal", {LD(0.0)}, {inf, 34, 0x0p+0L}, {inf, 34, 0x0p+0L}},
    {"ceil", {D(-2.5)}, {-0x8p-2L, 0, 0x0p+0L}, {-0x8p-2L, 0, 0x0p+0L}},
    {"ceilf", {F(-2.5)}, {-0x8p-2L, 0, 0x0p+0L}, {-0x8p-2L, 0, 0x0p+0L}},
    {"ceill", {LD(-2.5)}, {-0x8p-2L, 0, 0x0p+0L}, {-0x8p-2L, 0, 0x0p+0L}},
    {"floor", {D(-2.5)}, {-0xcp-2L, 0, 0x0p+0L}, {-0xcp-2L, 0, 0x0p+0L}},
    {"floorf", {F(-2.5)}, {-0xcp-2L, 0, 0x0p+0L}, {-0xcp-2L, 0, 0x0p+0L}},
    {"floorl", {LD(-2.5)}, {-0xcp-2L, 0, 0x0p+0L}, {-0xcp-2L, 0, 0x0p+0L}},
    {"nearbyint", {D(2.5)}, {0x8p-2L, 0, 0x0p+0L}, {0x8p-2L, 0, 0x0p+0L}},
    {"nearbyintf", {F(2.5)}, {0x8p-2L, 0, 0x0p+0L}, {0x8p-2L, 0, 0x0p+0L}},
    {"nearbyintl", {LD(2.5)}, {0x8p-2L, 0, 0x0p+0L}, {0x8p-2L, 0, 0x0p+0L}},
    {"rint", {D(3.5)}, {0x8p-1L, 0, 0x0p+0L}, {0x8p-1L, 0, 0x0p+0L}},
    {"rintf", {F(3.5)}, {0x8p-1L, 0, 0x0p+0L}, {0x8p-1L, 0, 0x0p+0L}},
    {"rintl", {LD(3.5)}, {0x8p-1L, 0, 0x0p+0L}, {0x8p-1L, 0, 0x0p+0L}},
    {"lrint", {D(2.5)}, {0x8p-2L, 0, 0x0p+0L}, {0x8p-2L, 0, 0x0p+0L}},
    {"lrint", {D(1e10)}, {0x9.502f9p+30L, 0, 0x0p+0L}, {-0x8p+28L, 0, 0x0p+0L}},
    {"lrintf", {F(2.5)}, {0x8p-2L, 0, 0x0p+0L}, {0x8p-2L, 0, 0x0p+0L}},
    {"lrintf", {F(1e10)}, {0x9.502f9p+30L, 0, 0x0p+0L}, {-0x8p+28L, 0, 0x0p+0L}},
    {"lrintl", {LD(2.5)}, {0x8p-2L, 0, 0x0p+0L}, {0x8p-2L, 0, 0x0p+0L}},
    {"lrintl", {LD(1e10)}, {0x9.502f9p+30L, 0, 0x0p+0L}, {-0x8p+28L, 0, 0x0p+0L}},
    {"llrint", {D(-3.5)}, {-0x8p-1L, 0, 0x0p+0L}, {-0x8p-1L, 0, 0x0p+0L}},
    {"llrintf", {F(-3.5)}, {-0x8p-1L, 0, 0x0p+0L}, {-0x8p-1L, 0, 0x0p+0L}},
    {"llrintl", {LD(-3.5)}, {-0x8p-1L, 0, 0x0p+0L}, {-0x8p-1L, 0, 0x0p+0L}},
    {"round", {D(-2.5)}, {-0xcp-2L, 0, 0x0p+0L}, {-0xcp-2L, 0, 0x0p+0L}},
    {"roundf", {F(-2.5)}, {-0xcp-2L, 0, 0x0p+0L}, {-0xcp-2L, 0, 0x0p+0L}},
    {"roundl", {LD(-2.5)}, {-0xcp-2L, 0, 0x0p+0L}, {-0xcp-2L, 0, 0x0p+0L}},
    {"lround", {D(2.5)}, {0xcp-2L, 0, 0x0p+0L}, {0xcp-2L, 0, 0x0p+0L}},
    {"lround", {D(-1e10)}, {-0x9.502f9p+30L, 0, 0x0p+0L}, {-0x8p+28L, 0, 0x0p+0L}},
    {"lroundf", {F(2.5)}, {0xcp-2L, 0, 0x0p+0L}, {0xcp-2L, 0, 0x0p+0L}},
    {"lroundf", {F(-1e10)}, {-0x9.502f9p+30L, 0, 0x0p+0L}, {-0x8p+28L, 0, 0x0p+0L}},
    {"lroundl", {LD(2.5)}, {0xcp-2L, 0, 0x0p+0L}, {0xcp-2L, 0, 0x0p+0L}},
    {"lroundl", {LD(-1e10)}, {-0x9.502f9p+30L, 0, 0x0p+0L}, {-0x8p+28L, 0, 0x0p+0L}},
    {"llround", {D(-3.5)}, {-0x8p-1L, 0, 0x0p+0L}, {-0x8p-1L, 0, 0x0p+0L}},
    {"llroundf", {F(-3.5)}, {-0x8p-1L, 0, 0x0p+0L}, {-0x8p-1L, 0, 0x0p+0L}},
    {"llroundl", {LD(-3.5)}, {-0x8p-1L, 0, 0x0p+0L}, {-0x8p-1L, 0, 0x0p+0L}},
    {"trunc", {D(-2.7)}, {-0x8p-2L, 0, 0x0p+0L}, {-0x8p-2L, 0, 0x0p+0L}},
    {"truncf", {F(-2.7)}, {-0x8p-2L, 0, 0x0p+0L}, {-0x8p-2L, 0, 0x0p+0L}},
    {"truncl", {LD(-2.7)}, {-0x8p-2L, 0, 0x0p+0L}, {-0x8p-2L, 0, 0x0p+0L}},
    {"fmod", {D(10.5), D(3.0)}, {0xcp-3L, 0, 0x0p+0L}, {0xcp-3L, 0, 0x0p+0L}},
    {"fmod", {D(1.0), D(0.0)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"fmodf", {F(10.5), F(3.0)}, {0xcp-3L, 0, 0x0p+0L}, {0xcp-3L, 0, 0x0p+0L}},
    {"fmodf", {F(1.0), F(0.0)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"fmodl", {LD(10.5), LD(3.0)}, {0xcp-3L, 0, 0x0p+0L}, {0xcp-3L, 0, 0x0p+0L}},
    {"fmodl", {LD(1.0), LD(0.0)}, {-nan, 33, 0x0p+0L}, {-nan, 33, 0x0p+0L}},
    {"remainder", {D(10.5), D(3.0)}, {-0xcp-3L, 0, 0x0p+0L}, {-0xcp-3L, 0, 0x0p+0L}},
    {"remainderf", {F(10.5), F(3.0)}, {-0xcp-3L, 0, 0x0p+0L}, {-0xcp-3L, 0, 0x0p+0L}},
    {"remainderl", {LD(10.5), LD(3.0)}, {-0xcp-3L, 0, 0x0p+0L}, {-0xcp-3L, 0, 0x0p+0L}},
    {"remquo", {D(10.5), D(3.0), OUT}, {-0xcp-3L, 0, 0x8p-1L}, {-0xcp-3L, 0, 0x8p-1L}},
    {"remquof", {F(10.5), F(3.0), OUT}, {-0xcp-3L, 0, 0x8p-1L}, {-0xcp-3L, 0, 0x8p-1L}},
    {"remquol", {LD(10.5), LD(3.0), OUT}, {-0xcp-3L, 0, 0x8p-1L}, {-0xcp-3L, 0, 0x8p-1L}},
    {"copysign", {D(2.0), D(-0.0)}, {-0x8p-2L, 0, 0x0p+0L}, {-0x8p-2L, 0, 0x0p+0L}},
    {"copysignf", {F(2.0), F(-0.0)}, {-0x8p-2L, 0, 0x0p+0L}, {-0x8p-2L, 0, 0x0p+0L}},
    {"copysignl", {LD(2.0), LD(-0.0)}, {-0x8p-2L, 0, 0x0p+0L}, {-0x8p-2L, 0, 0x0p+0L}},
    {"nextafter", {D(1.0), D(2.0)}, {0x8.0000000000008p-3L, 0, 0x0p+0L}, {0x8.0000000000008p-3L, 0, 0x0p+0L}},
    {"nextafterf", {F(1.0), F(2.0)}, {0x8.00001p-3L, 0, 0x0p+0L}, {0x8.00001p-3L, 0, 0x0p+0L}},
    {"nextafterl", {LD(1.0), LD(2.0)}, {0x8.000000000000001p-3L, 0, 0x0p+0L}, {0x8.000000000000001p-3L, 0, 0x0p+0L}},
    {"nexttoward", {D(1.0), LD(2.0)}, {0x8.0000000000008p-3L, 0, 0x0p+0L}, {0x8.0000000000008p-3L, 0, 0x0p+0L}},
    {"nexttowardf", {F(1.0), LD(2.0)}, {0x8.00001p-3L, 0, 0x0p+0L}, {0x8.00001p-3L, 0, 0x0p+0L}},
    {"nexttowardl", {LD(1.0), LD(2.0)}, {0x8.000000000000001p-3L, 0, 0x0p+0L}, {0x8.000000000000001p-3L, 0, 0x0p+0L}},
    {"fdim", {D(5.0), D(3.5)}, {0xcp-3L, 0, 0x0p+0L}, {0xcp-3L, 0, 0x0p+0L}},
    {"fdimf", {F(5.0), F(3.5)}, {0xcp-3L, 0, 0x0p+0L}, {0xcp-3L, 0, 0x0p+0L}},
    {"fdiml", {LD(5.0), LD(3.5)}, {0xcp-3L, 0, 0x0p+0L}, {0xcp-3L, 0, 0x0p+0L}},
    {"fmax", {D(1.0), D(nan)}, {0x8p-3L, 0, 0x0p+0L}, {0x8p-3L, 0, 0x0p+0L}},
    {"fmaxf", {F(1.0), F(nan)}, {0x8p-3L, 0, 0x0p+0L}, {0x8p-3L, 0, 0x0p+0L}},
    {"fmaxl", {LD(1.0), LD(nan)}, {0x8p-3L, 0, 0x0p+0L}, {0x8p-3L, 0, 0x0p+0L}},
    {"fmin", {D(-0.0), D(0.0)}, {0x0p+0L, 0, 0x0p+0L}, {0x0p+0L, 0, 0x0p+0L}},  // native -m32: -0x0p+0
    {"fminf", {F(-0.0), F(0.0)}, {0x0p+0L, 0, 0x0p+0L}, {0x0p+0L, 0, 0x0p+0L}}, // native -m32: -0x0p+0
    {"fminl", {LD(-0.0), LD(0.0)}, {-0x0p+0L, 0, 0x0p+0L}, {-0x0p+0L, 0, 0x0p+0L}},
    {"fma", {D(0.1), D(10.0), D(-1.0)}, {0x8p-57L, 0, 0x0p+0L}, {0x8p-57L, 0, 0x0p+0L}},
    {"fmaf", {F(0.1), F(10.0), F(-1.0)}, {0x8p-29L, 0, 0x0p+0L}, {0x8p-29L, 0, 0x0p+0L}},
    {"fmal", {LD(0.1), LD(10.0), LD(-1.0)}, {0x8p-57L, 0, 0x0p+0L}, {0x8p-57L, 0, 0x0p+0L}},
};

class MathTest : public LibraryTest {
protected:
    TypedValue make(const Argument& argument, Address out) {
        switch (argument.kind) {
        case Argument::Kind::Float:
            return value::Value::ofFloat(static_cast<float>(argument.number));
        case Argument::Kind::Double:
            return value::Value::ofDouble(static_cast<double>(argument.number));
        case Argument::Kind::LongDouble:
            return value::Value::ofLongDouble(argument.number);
        case Argument::Kind::Int:
            return intValue(static_cast<Int128>(argument.number));
        case Argument::Kind::Long:
            return integer(static_cast<Int128>(argument.number), ScalarType::Long);
        case Argument::Kind::Out:
            return pointer(out);
        }
        return value::Value();
    }

    /// The result of a call as long double.
    static long double number(const LibraryResult& result) {
        switch (result.value.kind()) {
        case value::Value::Kind::Float:
            return static_cast<long double>(result.value.asFloat());
        case value::Value::Kind::Double:
            return static_cast<long double>(result.value.asDouble());
        case value::Value::Kind::LongDouble:
            return result.value.asLongDouble();
        case value::Value::Kind::Integer:
            return static_cast<long double>(result.value.signedBits());
        default:
            ADD_FAILURE() << "no number was returned";
            return 0;
        }
    }

    /// What the function stored through its pointer argument, as long double.
    long double stored(std::string_view function, Address out) {
        if (function.starts_with("modf")) {
            const value::FloatFormat floatFormat = function == "modff"   ? value::FloatFormat::Float
                                                   : function == "modfl" ? value::FloatFormat::LongDouble
                                                                         : value::FloatFormat::Double;
            const std::size_t size = floatFormat == value::FloatFormat::Float    ? 4
                                     : floatFormat == value::FloatFormat::Double ? 8
                                                                                 : (lp64() ? 16 : 12);
            const value::Value integral = value::decodeFloat(readBytes(out, size), floatFormat);
            return number(LibraryResult::returning(integral));
        }
        return static_cast<long double>(readInteger(out, ScalarType::Int));
    }
};

INSTANTIATE_TEST_SUITE_P(DataModels, MathTest, testing::Values(DataModel::ILP32, DataModel::LP64), modelName);

/// Equal as numbers, with the same sign for zeros and NaNs, or both NaN.
testing::AssertionResult sameNumber(long double actual, long double expected) {
    const bool same = std::isnan(expected) ? std::isnan(actual) : actual == expected;
    if (same && std::signbit(actual) == std::signbit(expected)) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << std::hexfloat << actual << " instead of " << expected;
}

TEST_P(MathTest, ComputesLikeGlibc) {
    for (const MathCase& mathCase : mathCases) {
        SCOPED_TRACE(mathCase.function);
        const Outcome& expected = lp64() ? mathCase.expected64 : mathCase.expected32;
        const Address out = buffer(16);
        std::vector<TypedValue> arguments;
        for (const Argument& argument : mathCase.arguments) {
            arguments.push_back(make(argument, out));
        }
        writeInteger(context.objectAddress("errno"), 0, ScalarType::Int);
        const LibraryResult result = call(mathCase.function, arguments);
        ASSERT_EQ(result.kind, LibraryResult::Kind::Return) << result.message;
        EXPECT_TRUE(sameNumber(number(result), expected.result));
        EXPECT_EQ(errnoValue(), expected.error);
        if (mathCase.arguments.back().kind == Argument::Kind::Out) {
            EXPECT_TRUE(sameNumber(stored(mathCase.function, out), expected.stored));
        }
    }
}

TEST_P(MathTest, ResultsHaveTheTypeOfTheFunction) {
    EXPECT_EQ(call("sqrtf", {value::Value::ofFloat(4)}).value.kind(), value::Value::Kind::Float);
    EXPECT_EQ(call("sqrt", {value::Value::ofDouble(4)}).value.kind(), value::Value::Kind::Double);
    EXPECT_EQ(call("sqrtl", {value::Value::ofLongDouble(4)}).value.kind(), value::Value::Kind::LongDouble);
    EXPECT_EQ(call("lround", {value::Value::ofDouble(4)}).value.signedBits(), 4);
}

TEST_P(MathTest, ArgumentsConvertToTheParameterType) {
    EXPECT_EQ(call("sqrt", {value::Value::ofFloat(2.25f)}).value.asDouble(), 1.5);
    EXPECT_EQ(call("fabsf", {value::Value::ofDouble(-1e300)}).value.asFloat(), floatInf);
    EXPECT_TRUE(isUndefined(call("sqrt", {intValue(4)}), "not a floating-point number"));
    EXPECT_TRUE(isUndefined(call("pow", {value::Value::ofDouble(2)}), "too few arguments"));
}

TEST_P(MathTest, ErrnoIsOnlySetOnErrors) {
    writeInteger(context.objectAddress("errno"), 77, ScalarType::Int);
    call("sqrt", {value::Value::ofDouble(4)});
    EXPECT_EQ(errnoValue(), 77);
    call("log", {value::Value::ofDouble(0)});
    EXPECT_EQ(errnoValue(), ERANGE);
}

TEST_P(MathTest, PointerArgumentsAreChecked) {
    EXPECT_TRUE(isUndefined(call("frexp", {value::Value::ofDouble(1), pointer(0)}), "null pointer"));
    EXPECT_TRUE(isUndefined(call("modfl", {value::Value::ofLongDouble(1), pointer(buffer(8))}), "past the end"));
}

TEST_P(MathTest, NanReadsItsTag) {
    EXPECT_TRUE(std::isnan(call("nan", {pointer(string(""))}).value.asDouble()));
    EXPECT_TRUE(std::isnan(call("__builtin_nanf", {pointer(string("1"))}).value.asFloat()));
    EXPECT_TRUE(isUndefined(call("nanl", {pointer(0)}), "null pointer"));
}

// ---- Classification ----

TEST_P(MathTest, GlibcClassificationFunctions) {
    const value::Value notANumber = value::Value::ofDouble(doubleNan);
    const value::Value negativeInfinity = value::Value::ofDouble(-doubleInf);
    const value::Value one = value::Value::ofDouble(1);
    EXPECT_EQ(callInteger("__isnan", {notANumber}), 1);
    EXPECT_EQ(callInteger("__isnanf", {value::Value::ofFloat(1)}), 0);
    EXPECT_EQ(callInteger("__isinf", {negativeInfinity}), -1);
    EXPECT_EQ(callInteger("__isinfl", {value::Value::ofLongDouble(inf)}), 1);
    EXPECT_EQ(callInteger("__isinf", {one}), 0);
    EXPECT_EQ(callInteger("__finite", {notANumber}), 0);
    EXPECT_EQ(callInteger("__finitef", {value::Value::ofFloat(1)}), 1);
    EXPECT_EQ(callInteger("__fpclassify", {notANumber}), FP_NAN);
    EXPECT_EQ(callInteger("__fpclassify", {negativeInfinity}), FP_INFINITE);
    EXPECT_EQ(callInteger("__fpclassifyf", {value::Value::ofFloat(0)}), FP_ZERO);
    EXPECT_EQ(callInteger("__fpclassify", {value::Value::ofDouble(1e-310)}), FP_SUBNORMAL);
    EXPECT_EQ(callInteger("__fpclassifyl", {value::Value::ofLongDouble(1)}), FP_NORMAL);
}

// The values glibc 2.39's functions returned natively (-m64 and -m32), and those of GCC's builtins of the same name.

TEST_P(MathTest, GlibcIsnanlReturns65535) {
    for (const char* function : {"isnanl", "__isnanl"}) {
        EXPECT_EQ(callInteger(function, {value::Value::ofLongDouble(-nan)}), 65535) << function;
        EXPECT_EQ(callInteger(function, {value::Value::ofLongDouble(1)}), 0) << function;
    }
    EXPECT_EQ(callInteger("isnanf", {value::Value::ofFloat(floatNan)}), 1);
    EXPECT_EQ(callInteger("__builtin_isnanl", {value::Value::ofLongDouble(nan)}), 1);
    EXPECT_EQ(callInteger("__builtin_isnanf", {value::Value::ofFloat(floatNan)}), 1);
}

TEST_P(MathTest, GlibcIsinfGivesTheSignAndGccsBuiltinOne) {
    for (const char* function : {"isinf", "isinff", "isinfl", "__isinf", "__isinff", "__isinfl"}) {
        EXPECT_EQ(callInteger(function, {value::Value::ofDouble(-doubleInf)}), -1) << function;
    }
    for (const char* function : {"__builtin_isinf", "__builtin_isinff", "__builtin_isinfl"}) {
        EXPECT_EQ(callInteger(function, {value::Value::ofDouble(-doubleInf)}), 1) << function;
        EXPECT_EQ(callInteger(function, {value::Value::ofDouble(doubleInf)}), 1) << function;
        EXPECT_EQ(callInteger(function, {value::Value::ofDouble(1)}), 0) << function;
    }
}

TEST_P(MathTest, BuiltinFminAndFmaxChooseLikeGccsInlineCode) {
    const auto chooses = [&](const char* function, value::Value x, value::Value y) {
        const LibraryResult result = call(function, {std::move(x), std::move(y)});
        switch (result.value.kind()) {
        case value::Value::Kind::Float:
            return std::signbit(result.value.asFloat());
        case value::Value::Kind::Double:
            return std::signbit(result.value.asDouble());
        default:
            return std::signbit(result.value.asLongDouble());
        }
    };
    // Whether the result is negative: equal zeros give the first operand for float and double on x86-64, the second
    // otherwise; a NaN gives the other operand.
    EXPECT_EQ(chooses("__builtin_fmin", value::Value::ofDouble(-0.0), value::Value::ofDouble(0.0)), lp64());
    EXPECT_EQ(chooses("__builtin_fmaxf", value::Value::ofFloat(0.0f), value::Value::ofFloat(-0.0f)), !lp64());
    EXPECT_TRUE(chooses("__builtin_fminl", value::Value::ofLongDouble(0.0L), value::Value::ofLongDouble(-0.0L)));
    EXPECT_FALSE(chooses("__builtin_fmax", value::Value::ofDouble(-doubleNan), value::Value::ofDouble(doubleNan)));
    EXPECT_TRUE(chooses("__builtin_fmin", value::Value::ofDouble(doubleNan), value::Value::ofDouble(-doubleNan)));
    EXPECT_EQ(call("__builtin_fmin", {value::Value::ofDouble(1), value::Value::ofDouble(2)}).value.asDouble(), 1);
    EXPECT_EQ(
        call("__builtin_fmaxl", {value::Value::ofLongDouble(1), value::Value::ofLongDouble(2)}).value.asLongDouble(),
        2);
    EXPECT_EQ(call("__builtin_fmin", {value::Value::ofDouble(doubleNan), value::Value::ofDouble(3)}).value.asDouble(),
              3);
    // glibc's own fmin on x86-64 keeps the second zero.
    EXPECT_FALSE(chooses("fmin", value::Value::ofDouble(-0.0), value::Value::ofDouble(0.0)));
}

TEST_P(MathTest, MathMacrosAsFunctionsBehaveLikeGccsBuiltins) {
    EXPECT_EQ(callInteger("isfinite", {value::Value::ofFloat(1)}), 1);
    EXPECT_EQ(callInteger("isnormal", {value::Value::ofDouble(1e-310)}), 0);
    EXPECT_EQ(callInteger("signbit", {value::Value::ofDouble(-1)}), 1);
    EXPECT_EQ(callInteger("isless", {value::Value::ofDouble(1), value::Value::ofDouble(2)}), 1);
    EXPECT_EQ(callInteger("isunordered", {value::Value::ofDouble(doubleNan), value::Value::ofDouble(2)}), 1);
}

TEST_P(MathTest, GlibcSignbitReturnsItsImplementationsBits) {
    EXPECT_EQ(callInteger("__signbit", {value::Value::ofDouble(-0.0)}), lp64() ? 128 : 512);
    EXPECT_EQ(callInteger("__signbitf", {value::Value::ofFloat(-1)}), lp64() ? 8 : 512);
    EXPECT_EQ(callInteger("__signbitl", {value::Value::ofLongDouble(-inf)}), 512);
    EXPECT_EQ(callInteger("__signbit", {value::Value::ofDouble(1)}), 0);
}

TEST_P(MathTest, BuiltinSignbitReturnsWhatGccComputes) {
    EXPECT_EQ(callInteger("__builtin_signbit", {value::Value::ofDouble(-2.5)}), 1);
    EXPECT_EQ(callInteger("__builtin_signbit", {value::Value::ofFloat(-2.5f)}), INT_MIN);
    EXPECT_EQ(callInteger("__builtin_signbit", {value::Value::ofLongDouble(-2.5L)}), 512);
    EXPECT_EQ(callInteger("__builtin_signbit", {value::Value::ofDouble(2.5)}), 0);
    EXPECT_EQ(callInteger("__builtin_signbitf", {value::Value::ofFloat(-0.0f)}), INT_MIN);
    EXPECT_EQ(callInteger("__builtin_signbitl", {value::Value::ofLongDouble(-nan)}), 512);
}

TEST_P(MathTest, TypeGenericBuiltinsUseTheArgumentsFormat) {
    EXPECT_EQ(callInteger("__builtin_isnan", {value::Value::ofFloat(floatNan)}), 1);
    EXPECT_EQ(callInteger("__builtin_isnan", {value::Value::ofLongDouble(1)}), 0);
    EXPECT_EQ(callInteger("__builtin_isinf", {value::Value::ofDouble(-doubleInf)}), 1);
    EXPECT_EQ(callInteger("__builtin_isinf_sign", {value::Value::ofDouble(-doubleInf)}), -1);
    EXPECT_EQ(callInteger("__builtin_isinf_sign", {value::Value::ofFloat(floatInf)}), 1);
    EXPECT_EQ(callInteger("__builtin_isfinite", {value::Value::ofLongDouble(inf)}), 0);
    EXPECT_EQ(callInteger("__builtin_isnormal", {value::Value::ofDouble(1e-310)}), 0);
    EXPECT_EQ(callInteger("__builtin_isnormal", {value::Value::ofFloat(1)}), 1);
    // A double that is subnormal as float only after a conversion stays normal.
    EXPECT_EQ(callInteger("__builtin_isnormal", {value::Value::ofDouble(1e-40)}), 1);
    EXPECT_TRUE(isUndefined(call("__builtin_isnan", {intValue(0)}), "not a floating-point number"));
}

TEST_P(MathTest, BuiltinFpclassifyReturnsTheArgumentForTheClass) {
    const auto classify = [&](value::Value x) {
        return callInteger("__builtin_fpclassify",
                           {intValue(10), intValue(11), intValue(12), intValue(13), intValue(14), std::move(x)});
    };
    EXPECT_EQ(classify(value::Value::ofDouble(doubleNan)), 10);
    EXPECT_EQ(classify(value::Value::ofFloat(-floatInf)), 11);
    EXPECT_EQ(classify(value::Value::ofDouble(1)), 12);
    EXPECT_EQ(classify(value::Value::ofDouble(1e-310)), 13);
    EXPECT_EQ(classify(value::Value::ofLongDouble(-0.0L)), 14);
}

TEST_P(MathTest, QuietComparisons) {
    const value::Value notANumber = value::Value::ofDouble(doubleNan);
    const value::Value one = value::Value::ofDouble(1);
    const value::Value two = value::Value::ofFloat(2);
    EXPECT_EQ(callInteger("__builtin_isgreater", {two, one}), 1);
    EXPECT_EQ(callInteger("__builtin_isgreater", {notANumber, one}), 0);
    EXPECT_EQ(callInteger("__builtin_isgreaterequal", {one, one}), 1);
    EXPECT_EQ(callInteger("__builtin_isless", {one, two}), 1);
    EXPECT_EQ(callInteger("__builtin_islessequal", {notANumber, notANumber}), 0);
    EXPECT_EQ(callInteger("__builtin_islessgreater", {one, two}), 1);
    EXPECT_EQ(callInteger("__builtin_islessgreater", {one, one}), 0);
    EXPECT_EQ(callInteger("__builtin_isunordered", {one, notANumber}), 1);
}

TEST_P(MathTest, InfinityBuiltins) {
    EXPECT_EQ(call("__builtin_inf", {}).value.asDouble(), doubleInf);
    EXPECT_EQ(call("__builtin_inff", {}).value.asFloat(), floatInf);
    EXPECT_EQ(call("__builtin_huge_vall", {}).value.asLongDouble(), inf);
}

TEST_P(MathTest, BuiltinFormsOfTheFunctions) {
    EXPECT_EQ(call("__builtin_fabs", {value::Value::ofDouble(-3)}).value.asDouble(), 3);
    EXPECT_EQ(call("__builtin_sqrtf", {value::Value::ofFloat(9)}).value.asFloat(), 3);
}

TEST_P(MathTest, OnlyRoundToNearestIsSupported) {
    EXPECT_EQ(callInteger("fegetround", {}), FE_TONEAREST);
    EXPECT_EQ(callInteger("fesetround", {intValue(FE_TONEAREST)}), 0);
    EXPECT_TRUE(endedWith(call("fesetround", {intValue(FE_UPWARD)}), LibraryResult::Kind::Unsupported));
}

} // namespace
} // namespace nitwit::interp::test
