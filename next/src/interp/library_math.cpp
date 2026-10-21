// <math.h> (C17 7.12) with the host's libm, which is glibc's: for float, double and long double the results are those
// of a native run. glibc's classification functions and GCC's floating-point builtins.

#include <cerrno>
#include <cfenv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include "interp/library_call.hpp"

namespace nitwit::interp::library {

namespace {

// ---- Arguments and results ----

template <typename T>
constexpr value::FloatFormat formatOf() {
    if constexpr (std::is_same_v<T, float>) {
        return value::FloatFormat::Float;
    } else if constexpr (std::is_same_v<T, double>) {
        return value::FloatFormat::Double;
    } else {
        return value::FloatFormat::LongDouble;
    }
}

/// The floating argument at `index`, converted to the parameter type T.
template <typename T>
T argument(const Call& call, std::size_t index) {
    const value::Value converted = call.floatArgument(index, formatOf<T>());
    if constexpr (std::is_same_v<T, float>) {
        return converted.asFloat();
    } else if constexpr (std::is_same_v<T, double>) {
        return converted.asDouble();
    } else {
        return converted.asLongDouble();
    }
}

template <typename T>
value::Value valueOf(T x) {
    if constexpr (std::is_same_v<T, float>) {
        return value::Value::ofFloat(x);
    } else if constexpr (std::is_same_v<T, double>) {
        return value::Value::ofDouble(x);
    } else {
        return value::Value::ofLongDouble(x);
    }
}

template <typename T>
LibraryResult returnFloat(T result) {
    return LibraryResult::returning(valueOf(result));
}

/// Calls a host libm function and passes the errno it sets on to the program: glibc reports domain, pole and range
/// errors through errno (C17 7.12.1, math_errhandling is MATH_ERRNO | MATH_ERREXCEPT).
template <typename Function>
auto withErrno(const Call& call, Function function) {
    errno = 0;
    const auto result = function();
    if (const int error = errno; error != 0) {
        call.setErrno(error);
    }
    return result;
}

// ---- The functions by signature; T is the parameter type, float, double or long double ----

template <typename T, T (*function)(T)>
LibraryResult unary(const Call& call) {
    const T x = argument<T>(call, 0);
    return returnFloat(withErrno(call, [&] { return function(x); }));
}

template <typename T, T (*function)(T, T)>
LibraryResult binary(const Call& call) {
    const T x = argument<T>(call, 0);
    const T y = argument<T>(call, 1);
    return returnFloat(withErrno(call, [&] { return function(x, y); }));
}

template <typename T, T (*function)(T, T, T)>
LibraryResult ternary(const Call& call) {
    const T x = argument<T>(call, 0);
    const T y = argument<T>(call, 1);
    const T z = argument<T>(call, 2);
    return returnFloat(withErrno(call, [&] { return function(x, y, z); }));
}

/// ldexp, scalbn (int exponent) and scalbln (long exponent).
template <typename T, typename Exponent, T (*function)(T, Exponent)>
LibraryResult withExponent(const Call& call) {
    const T x = argument<T>(call, 0);
    const auto exponent = static_cast<Exponent>(
        call.signedArgument(1, std::is_same_v<Exponent, int> ? ScalarType::Int : ScalarType::Long));
    return returnFloat(withErrno(call, [&] { return function(x, exponent); }));
}

/// nexttoward: the second parameter is a long double for every T.
template <typename T, T (*function)(T, long double)>
LibraryResult toward(const Call& call) {
    const T x = argument<T>(call, 0);
    const auto y = argument<long double>(call, 1);
    return returnFloat(withErrno(call, [&] { return function(x, y); }));
}

/// lrint and lround return a long. On i386, a result outside the range of the 32-bit long is 0x80000000, the
/// "integer indefinite" value of x87 conversions; glibc's x86-64 functions give LONG_MIN in the same way.
template <typename T, long (*function)(T)>
LibraryResult toLong(const Call& call) {
    const T x = argument<T>(call, 0);
    long result = withErrno(call, [&] { return function(x); });
    if (call.target().dataModel() == DataModel::ILP32 && (result < INT32_MIN || result > INT32_MAX)) {
        result = INT32_MIN;
    }
    return call.returnInteger(static_cast<UInt128>(Int128{result}), ScalarType::Long);
}

template <typename T, long long (*function)(T)>
LibraryResult toLongLong(const Call& call) {
    const T x = argument<T>(call, 0);
    const long long result = withErrno(call, [&] { return function(x); });
    return call.returnInteger(static_cast<UInt128>(Int128{result}), ScalarType::LongLong);
}

template <typename T, int (*function)(T)>
LibraryResult toInt(const Call& call) {
    const T x = argument<T>(call, 0);
    return call.returnInt(withErrno(call, [&] { return function(x); }));
}

/// frexp(x, &exponent).
template <typename T, T (*function)(T, int*)>
LibraryResult frexp(const Call& call) {
    const T x = argument<T>(call, 0);
    const Address exponentAddress = call.pointerArgument(1);
    int exponent = 0;
    const T result = function(x, &exponent);
    call.writeInteger(exponentAddress, static_cast<UInt128>(Int128{exponent}), ScalarType::Int);
    return returnFloat(result);
}

/// remquo(x, y, &quotient).
template <typename T, T (*function)(T, T, int*)>
LibraryResult remquo(const Call& call) {
    const T x = argument<T>(call, 0);
    const T y = argument<T>(call, 1);
    const Address quotientAddress = call.pointerArgument(2);
    int quotient = 0;
    const T result = withErrno(call, [&] { return function(x, y, &quotient); });
    call.writeInteger(quotientAddress, static_cast<UInt128>(Int128{quotient}), ScalarType::Int);
    return returnFloat(result);
}

/// modf(x, &integralPart).
template <typename T, T (*function)(T, T*)>
LibraryResult modf(const Call& call) {
    const T x = argument<T>(call, 0);
    const Address integralAddress = call.pointerArgument(1);
    T integral = 0;
    const T result = function(x, &integral);
    // A long double object has padding after its 10 bytes: 2 on i386, 6 on x86-64.
    const std::size_t size =
        std::is_same_v<T, long double> ? call.target().layout(ScalarType::LongDouble).size : sizeof(T);
    std::vector<std::byte> bytes(size);
    value::encodeFloat(valueOf(integral), formatOf<T>(), bytes);
    call.write(integralAddress, bytes);
    return returnFloat(result);
}

/// nan(tag) and __builtin_nan(tag): a quiet NaN whose payload the string selects (C17 7.12.11.2).
template <typename T, T (*function)(const char*)>
LibraryResult nan(const Call& call) {
    const std::string tag = call.readString(call.pointerArgument(0));
    return returnFloat(function(tag.c_str()));
}

template <typename T>
LibraryResult infinity(const Call& /*call*/) {
    return returnFloat(std::numeric_limits<T>::infinity());
}

// ---- Classification ----

/// Calls `function` with the floating argument at `index` in its own format, for the type-generic builtins.
template <typename Function>
int visitFloat(const Call& call, std::size_t index, Function function) {
    const value::Value& argument = call.argument(index);
    switch (argument.kind()) {
    case value::Value::Kind::Float:
        return function(argument.asFloat());
    case value::Value::Kind::Double:
        return function(argument.asDouble());
    case value::Value::Kind::LongDouble:
        return function(argument.asLongDouble());
    default:
        call.undefinedBehavior(std::format("argument {} of {} is not a floating-point number", index + 1, call.name()));
    }
}

struct IsNan {
    int operator()(auto x) const { return std::isnan(x) ? 1 : 0; }
};

/// GCC's __builtin_isinf: 1 for both infinities.
struct IsInf {
    int operator()(auto x) const { return std::isinf(x) ? 1 : 0; }
};

/// glibc's __isinf and GCC's __builtin_isinf_sign: -1 for negative infinity.
struct IsInfSign {
    int operator()(auto x) const {
        if (!std::isinf(x)) {
            return 0;
        }
        return std::signbit(x) ? -1 : 1;
    }
};

struct IsFinite {
    int operator()(auto x) const { return std::isfinite(x) ? 1 : 0; }
};

struct IsNormal {
    int operator()(auto x) const { return std::isnormal(x) ? 1 : 0; }
};

/// FP_NAN, FP_INFINITE, FP_ZERO, FP_SUBNORMAL or FP_NORMAL; the host's values are glibc's.
struct FpClassify {
    int operator()(auto x) const { return std::fpclassify(x); }
};

/// GCC's __builtin_signbit at -O0 returns the sign bit where its code finds it: bit 0 for double, bit 31 for float,
/// bit 9 (the x87 C1 flag of fxam) for long double. Measured with gcc-14 on x86-64 and i386 with SSE.
struct BuiltinSignBit {
    template <typename T>
    int operator()(T x) const {
        if (!std::signbit(x)) {
            return 0;
        }
        if constexpr (std::is_same_v<T, float>) {
            return INT_MIN;
        } else if constexpr (std::is_same_v<T, double>) {
            return 1;
        } else {
            return 512;
        }
    }
};

template <typename Predicate>
LibraryResult generic(const Call& call) {
    return call.returnInt(visitFloat(call, 0, Predicate{}));
}

template <typename T, typename Predicate>
LibraryResult typed(const Call& call) {
    return call.returnInt(Predicate{}(argument<T>(call, 0)));
}

/// glibc's __signbit functions return a nonzero value that depends on their implementation: 8 (float), 128 (double)
/// and 512 (long double) on x86-64, 512 on i386.
template <typename T>
LibraryResult signbit(const Call& call) {
    if (!std::signbit(argument<T>(call, 0))) {
        return call.returnInt(0);
    }
    if (call.target().dataModel() == DataModel::ILP32 || std::is_same_v<T, long double>) {
        return call.returnInt(512);
    }
    return call.returnInt(std::is_same_v<T, float> ? 8 : 128);
}

/// __builtin_fpclassify(nan, infinite, normal, subnormal, zero, x) returns the argument for the class of x.
LibraryResult builtinFpclassify(const Call& call) {
    const int category = visitFloat(call, 5, FpClassify{});
    switch (category) {
    case FP_NAN:
        return call.returnInt(call.intArgument(0));
    case FP_INFINITE:
        return call.returnInt(call.intArgument(1));
    case FP_NORMAL:
        return call.returnInt(call.intArgument(2));
    case FP_SUBNORMAL:
        return call.returnInt(call.intArgument(3));
    default:
        return call.returnInt(call.intArgument(4));
    }
}

/// The comparison macros of C17 7.12.14, which raise no exception for NaNs. Both arguments convert exactly to long
/// double, so comparing there gives the result of their common type.
template <bool (*compare)(long double, long double)>
LibraryResult quietComparison(const Call& call) {
    return call.returnInt(compare(argument<long double>(call, 0), argument<long double>(call, 1)) ? 1 : 0);
}

bool isGreater(long double x, long double y) {
    return std::isgreater(x, y);
}
bool isGreaterEqual(long double x, long double y) {
    return std::isgreaterequal(x, y);
}
bool isLess(long double x, long double y) {
    return std::isless(x, y);
}
bool isLessEqual(long double x, long double y) {
    return std::islessequal(x, y);
}
bool isLessGreater(long double x, long double y) {
    return std::islessgreater(x, y);
}
bool isUnordered(long double x, long double y) {
    return std::isunordered(x, y);
}

// ---- The floating-point environment (C17 7.6) ----

/// The interpreter computes with round-to-nearest only.
LibraryResult fegetround(const Call& call) {
    return call.returnInt(FE_TONEAREST);
}

LibraryResult fesetround(const Call& call) {
    if (call.intArgument(0) != FE_TONEAREST) {
        call.unsupported(std::format("{}: rounding modes other than to nearest are not supported", call.name()));
    }
    return call.returnInt(0);
}

// One row per function and type: the C function of <math.h> on the host is the implementation.
constexpr FunctionEntry functions[] = {
    // Trigonometric and hyperbolic functions
    {"acos", entryPoint<unary<double, ::acos>>},
    {"acosf", entryPoint<unary<float, ::acosf>>},
    {"acosl", entryPoint<unary<long double, ::acosl>>},
    {"asin", entryPoint<unary<double, ::asin>>},
    {"asinf", entryPoint<unary<float, ::asinf>>},
    {"asinl", entryPoint<unary<long double, ::asinl>>},
    {"atan", entryPoint<unary<double, ::atan>>},
    {"atanf", entryPoint<unary<float, ::atanf>>},
    {"atanl", entryPoint<unary<long double, ::atanl>>},
    {"atan2", entryPoint<binary<double, ::atan2>>},
    {"atan2f", entryPoint<binary<float, ::atan2f>>},
    {"atan2l", entryPoint<binary<long double, ::atan2l>>},
    {"cos", entryPoint<unary<double, ::cos>>},
    {"cosf", entryPoint<unary<float, ::cosf>>},
    {"cosl", entryPoint<unary<long double, ::cosl>>},
    {"sin", entryPoint<unary<double, ::sin>>},
    {"sinf", entryPoint<unary<float, ::sinf>>},
    {"sinl", entryPoint<unary<long double, ::sinl>>},
    {"tan", entryPoint<unary<double, ::tan>>},
    {"tanf", entryPoint<unary<float, ::tanf>>},
    {"tanl", entryPoint<unary<long double, ::tanl>>},
    {"acosh", entryPoint<unary<double, ::acosh>>},
    {"acoshf", entryPoint<unary<float, ::acoshf>>},
    {"acoshl", entryPoint<unary<long double, ::acoshl>>},
    {"asinh", entryPoint<unary<double, ::asinh>>},
    {"asinhf", entryPoint<unary<float, ::asinhf>>},
    {"asinhl", entryPoint<unary<long double, ::asinhl>>},
    {"atanh", entryPoint<unary<double, ::atanh>>},
    {"atanhf", entryPoint<unary<float, ::atanhf>>},
    {"atanhl", entryPoint<unary<long double, ::atanhl>>},
    {"cosh", entryPoint<unary<double, ::cosh>>},
    {"coshf", entryPoint<unary<float, ::coshf>>},
    {"coshl", entryPoint<unary<long double, ::coshl>>},
    {"sinh", entryPoint<unary<double, ::sinh>>},
    {"sinhf", entryPoint<unary<float, ::sinhf>>},
    {"sinhl", entryPoint<unary<long double, ::sinhl>>},
    {"tanh", entryPoint<unary<double, ::tanh>>},
    {"tanhf", entryPoint<unary<float, ::tanhf>>},
    {"tanhl", entryPoint<unary<long double, ::tanhl>>},
    // Exponential and logarithmic functions
    {"exp", entryPoint<unary<double, ::exp>>},
    {"expf", entryPoint<unary<float, ::expf>>},
    {"expl", entryPoint<unary<long double, ::expl>>},
    {"exp2", entryPoint<unary<double, ::exp2>>},
    {"exp2f", entryPoint<unary<float, ::exp2f>>},
    {"exp2l", entryPoint<unary<long double, ::exp2l>>},
    {"expm1", entryPoint<unary<double, ::expm1>>},
    {"expm1f", entryPoint<unary<float, ::expm1f>>},
    {"expm1l", entryPoint<unary<long double, ::expm1l>>},
    {"frexp", entryPoint<frexp<double, ::frexp>>},
    {"frexpf", entryPoint<frexp<float, ::frexpf>>},
    {"frexpl", entryPoint<frexp<long double, ::frexpl>>},
    {"ilogb", entryPoint<toInt<double, ::ilogb>>},
    {"ilogbf", entryPoint<toInt<float, ::ilogbf>>},
    {"ilogbl", entryPoint<toInt<long double, ::ilogbl>>},
    {"ldexp", entryPoint<withExponent<double, int, ::ldexp>>},
    {"ldexpf", entryPoint<withExponent<float, int, ::ldexpf>>},
    {"ldexpl", entryPoint<withExponent<long double, int, ::ldexpl>>},
    {"log", entryPoint<unary<double, ::log>>},
    {"logf", entryPoint<unary<float, ::logf>>},
    {"logl", entryPoint<unary<long double, ::logl>>},
    {"log10", entryPoint<unary<double, ::log10>>},
    {"log10f", entryPoint<unary<float, ::log10f>>},
    {"log10l", entryPoint<unary<long double, ::log10l>>},
    {"log1p", entryPoint<unary<double, ::log1p>>},
    {"log1pf", entryPoint<unary<float, ::log1pf>>},
    {"log1pl", entryPoint<unary<long double, ::log1pl>>},
    {"log2", entryPoint<unary<double, ::log2>>},
    {"log2f", entryPoint<unary<float, ::log2f>>},
    {"log2l", entryPoint<unary<long double, ::log2l>>},
    {"logb", entryPoint<unary<double, ::logb>>},
    {"logbf", entryPoint<unary<float, ::logbf>>},
    {"logbl", entryPoint<unary<long double, ::logbl>>},
    {"modf", entryPoint<modf<double, ::modf>>},
    {"modff", entryPoint<modf<float, ::modff>>},
    {"modfl", entryPoint<modf<long double, ::modfl>>},
    {"scalbn", entryPoint<withExponent<double, int, ::scalbn>>},
    {"scalbnf", entryPoint<withExponent<float, int, ::scalbnf>>},
    {"scalbnl", entryPoint<withExponent<long double, int, ::scalbnl>>},
    {"scalbln", entryPoint<withExponent<double, long, ::scalbln>>},
    {"scalblnf", entryPoint<withExponent<float, long, ::scalblnf>>},
    {"scalblnl", entryPoint<withExponent<long double, long, ::scalblnl>>},
    // Power and absolute-value functions
    {"cbrt", entryPoint<unary<double, ::cbrt>>},
    {"cbrtf", entryPoint<unary<float, ::cbrtf>>},
    {"cbrtl", entryPoint<unary<long double, ::cbrtl>>},
    {"fabs", entryPoint<unary<double, ::fabs>>},
    {"fabsf", entryPoint<unary<float, ::fabsf>>},
    {"fabsl", entryPoint<unary<long double, ::fabsl>>},
    {"hypot", entryPoint<binary<double, ::hypot>>},
    {"hypotf", entryPoint<binary<float, ::hypotf>>},
    {"hypotl", entryPoint<binary<long double, ::hypotl>>},
    {"pow", entryPoint<binary<double, ::pow>>},
    {"powf", entryPoint<binary<float, ::powf>>},
    {"powl", entryPoint<binary<long double, ::powl>>},
    {"sqrt", entryPoint<unary<double, ::sqrt>>},
    {"sqrtf", entryPoint<unary<float, ::sqrtf>>},
    {"sqrtl", entryPoint<unary<long double, ::sqrtl>>},
    // Error and gamma functions
    {"erf", entryPoint<unary<double, ::erf>>},
    {"erff", entryPoint<unary<float, ::erff>>},
    {"erfl", entryPoint<unary<long double, ::erfl>>},
    {"erfc", entryPoint<unary<double, ::erfc>>},
    {"erfcf", entryPoint<unary<float, ::erfcf>>},
    {"erfcl", entryPoint<unary<long double, ::erfcl>>},
    {"lgamma", entryPoint<unary<double, ::lgamma>>},
    {"lgammaf", entryPoint<unary<float, ::lgammaf>>},
    {"lgammal", entryPoint<unary<long double, ::lgammal>>},
    {"tgamma", entryPoint<unary<double, ::tgamma>>},
    {"tgammaf", entryPoint<unary<float, ::tgammaf>>},
    {"tgammal", entryPoint<unary<long double, ::tgammal>>},
    // Nearest integer functions
    {"ceil", entryPoint<unary<double, ::ceil>>},
    {"ceilf", entryPoint<unary<float, ::ceilf>>},
    {"ceill", entryPoint<unary<long double, ::ceill>>},
    {"floor", entryPoint<unary<double, ::floor>>},
    {"floorf", entryPoint<unary<float, ::floorf>>},
    {"floorl", entryPoint<unary<long double, ::floorl>>},
    {"nearbyint", entryPoint<unary<double, ::nearbyint>>},
    {"nearbyintf", entryPoint<unary<float, ::nearbyintf>>},
    {"nearbyintl", entryPoint<unary<long double, ::nearbyintl>>},
    {"rint", entryPoint<unary<double, ::rint>>},
    {"rintf", entryPoint<unary<float, ::rintf>>},
    {"rintl", entryPoint<unary<long double, ::rintl>>},
    {"lrint", entryPoint<toLong<double, ::lrint>>},
    {"lrintf", entryPoint<toLong<float, ::lrintf>>},
    {"lrintl", entryPoint<toLong<long double, ::lrintl>>},
    {"llrint", entryPoint<toLongLong<double, ::llrint>>},
    {"llrintf", entryPoint<toLongLong<float, ::llrintf>>},
    {"llrintl", entryPoint<toLongLong<long double, ::llrintl>>},
    {"round", entryPoint<unary<double, ::round>>},
    {"roundf", entryPoint<unary<float, ::roundf>>},
    {"roundl", entryPoint<unary<long double, ::roundl>>},
    {"lround", entryPoint<toLong<double, ::lround>>},
    {"lroundf", entryPoint<toLong<float, ::lroundf>>},
    {"lroundl", entryPoint<toLong<long double, ::lroundl>>},
    {"llround", entryPoint<toLongLong<double, ::llround>>},
    {"llroundf", entryPoint<toLongLong<float, ::llroundf>>},
    {"llroundl", entryPoint<toLongLong<long double, ::llroundl>>},
    {"trunc", entryPoint<unary<double, ::trunc>>},
    {"truncf", entryPoint<unary<float, ::truncf>>},
    {"truncl", entryPoint<unary<long double, ::truncl>>},
    // Remainder functions
    {"fmod", entryPoint<binary<double, ::fmod>>},
    {"fmodf", entryPoint<binary<float, ::fmodf>>},
    {"fmodl", entryPoint<binary<long double, ::fmodl>>},
    {"remainder", entryPoint<binary<double, ::remainder>>},
    {"remainderf", entryPoint<binary<float, ::remainderf>>},
    {"remainderl", entryPoint<binary<long double, ::remainderl>>},
    {"remquo", entryPoint<remquo<double, ::remquo>>},
    {"remquof", entryPoint<remquo<float, ::remquof>>},
    {"remquol", entryPoint<remquo<long double, ::remquol>>},
    // Manipulation functions
    {"copysign", entryPoint<binary<double, ::copysign>>},
    {"copysignf", entryPoint<binary<float, ::copysignf>>},
    {"copysignl", entryPoint<binary<long double, ::copysignl>>},
    {"nan", entryPoint<nan<double, ::nan>>},
    {"nanf", entryPoint<nan<float, ::nanf>>},
    {"nanl", entryPoint<nan<long double, ::nanl>>},
    {"nextafter", entryPoint<binary<double, ::nextafter>>},
    {"nextafterf", entryPoint<binary<float, ::nextafterf>>},
    {"nextafterl", entryPoint<binary<long double, ::nextafterl>>},
    {"nexttoward", entryPoint<toward<double, ::nexttoward>>},
    {"nexttowardf", entryPoint<toward<float, ::nexttowardf>>},
    {"nexttowardl", entryPoint<toward<long double, ::nexttowardl>>},
    // Maximum, minimum and positive difference functions; floating multiply-add
    {"fdim", entryPoint<binary<double, ::fdim>>},
    {"fdimf", entryPoint<binary<float, ::fdimf>>},
    {"fdiml", entryPoint<binary<long double, ::fdiml>>},
    {"fmax", entryPoint<binary<double, ::fmax>>},
    {"fmaxf", entryPoint<binary<float, ::fmaxf>>},
    {"fmaxl", entryPoint<binary<long double, ::fmaxl>>},
    {"fmin", entryPoint<binary<double, ::fmin>>},
    {"fminf", entryPoint<binary<float, ::fminf>>},
    {"fminl", entryPoint<binary<long double, ::fminl>>},
    {"fma", entryPoint<ternary<double, ::fma>>},
    {"fmaf", entryPoint<ternary<float, ::fmaf>>},
    {"fmal", entryPoint<ternary<long double, ::fmal>>},
    // glibc's classification functions, which its <math.h> macros used before GCC had builtins for them
    {"__fpclassify", entryPoint<typed<double, FpClassify>>},
    {"__fpclassifyf", entryPoint<typed<float, FpClassify>>},
    {"__fpclassifyl", entryPoint<typed<long double, FpClassify>>},
    {"__isnan", entryPoint<typed<double, IsNan>>},
    {"__isnanf", entryPoint<typed<float, IsNan>>},
    {"__isnanl", entryPoint<typed<long double, IsNan>>},
    {"isnan", entryPoint<typed<double, IsNan>>},
    {"isnanf", entryPoint<typed<float, IsNan>>},
    {"isnanl", entryPoint<typed<long double, IsNan>>},
    {"__isinf", entryPoint<typed<double, IsInfSign>>},
    {"__isinff", entryPoint<typed<float, IsInfSign>>},
    {"__isinfl", entryPoint<typed<long double, IsInfSign>>},
    {"isinf", entryPoint<typed<double, IsInfSign>>},
    {"isinff", entryPoint<typed<float, IsInfSign>>},
    {"isinfl", entryPoint<typed<long double, IsInfSign>>},
    {"__finite", entryPoint<typed<double, IsFinite>>},
    {"__finitef", entryPoint<typed<float, IsFinite>>},
    {"__finitel", entryPoint<typed<long double, IsFinite>>},
    {"finite", entryPoint<typed<double, IsFinite>>},
    {"finitef", entryPoint<typed<float, IsFinite>>},
    {"finitel", entryPoint<typed<long double, IsFinite>>},
    {"__signbit", entryPoint<signbit<double>>},
    {"__signbitf", entryPoint<signbit<float>>},
    {"__signbitl", entryPoint<signbit<long double>>},
    // GCC's builtins; the type-generic ones take the argument in its own format
    {"__builtin_isnan", entryPoint<generic<IsNan>>},
    {"__builtin_isinf", entryPoint<generic<IsInf>>},
    {"__builtin_isinf_sign", entryPoint<generic<IsInfSign>>},
    {"__builtin_isfinite", entryPoint<generic<IsFinite>>},
    {"__builtin_isnormal", entryPoint<generic<IsNormal>>},
    {"__builtin_signbit", entryPoint<generic<BuiltinSignBit>>},
    {"__builtin_signbitf", entryPoint<typed<float, BuiltinSignBit>>},
    {"__builtin_signbitl", entryPoint<typed<long double, BuiltinSignBit>>},
    {"__builtin_fpclassify", entryPoint<builtinFpclassify>},
    {"__builtin_isgreater", entryPoint<quietComparison<isGreater>>},
    {"__builtin_isgreaterequal", entryPoint<quietComparison<isGreaterEqual>>},
    {"__builtin_isless", entryPoint<quietComparison<isLess>>},
    {"__builtin_islessequal", entryPoint<quietComparison<isLessEqual>>},
    {"__builtin_islessgreater", entryPoint<quietComparison<isLessGreater>>},
    {"__builtin_isunordered", entryPoint<quietComparison<isUnordered>>},
    {"__builtin_inf", entryPoint<infinity<double>>},
    {"__builtin_inff", entryPoint<infinity<float>>},
    {"__builtin_infl", entryPoint<infinity<long double>>},
    {"__builtin_huge_val", entryPoint<infinity<double>>},
    {"__builtin_huge_valf", entryPoint<infinity<float>>},
    {"__builtin_huge_vall", entryPoint<infinity<long double>>},
    {"fegetround", entryPoint<fegetround>},
    {"fesetround", entryPoint<fesetround>},
};

} // namespace

std::span<const FunctionEntry> mathFunctions() {
    return functions;
}

} // namespace nitwit::interp::library
