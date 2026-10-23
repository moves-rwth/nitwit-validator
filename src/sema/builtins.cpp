// The builtin functions of GCC that sema types (a subset of gcc/builtins.def): their signatures, written in C, and the
// type-generic builtins whose calls sema checks itself.

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ast/casting.hpp"
#include "sema/sema.hpp"
#include "sema/sema_impl.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::sema::detail {

namespace {

using ast::BuiltinKind;

enum class Origin : std::uint8_t {
    Library, // a C library function GCC knows as a builtin: both `name` and `__builtin_name`
    Builtin, // only `__builtin_name`
    Gnu,     // a GNU builtin with a name of its own, without the prefix (`__atomic_thread_fence`)
};

struct Entry {
    std::string_view name; // without "__builtin_"
    std::string_view signature;
    Origin origin;
    GenericBuiltin generic;
    bool isNoreturn;
    bool hasMathVariants; // `namef` and `namel` exist, with float and long double for double
};

constexpr Entry library(std::string_view name, std::string_view signature, bool isNoreturn = false) {
    return Entry{name, signature, Origin::Library, GenericBuiltin::None, isNoreturn, false};
}

constexpr Entry builtin(std::string_view name, std::string_view signature, bool isNoreturn = false) {
    return Entry{name, signature, Origin::Builtin, GenericBuiltin::None, isNoreturn, false};
}

constexpr Entry math(std::string_view name, std::string_view signature, Origin origin) {
    return Entry{name, signature, origin, GenericBuiltin::None, false, true};
}

constexpr Entry generic(std::string_view name, GenericBuiltin kind, Origin origin) {
    return Entry{name, "", origin, kind, false, false};
}

constexpr Entry gnu(std::string_view name, std::string_view signature) {
    return Entry{name, signature, Origin::Gnu, GenericBuiltin::None, false, false};
}

// Signatures use the C spelling of types plus size_t, uint16, uint32, uint64, va_list and FILE (as void).
constexpr Entry entries[] = {
    // Memory and strings.
    library("malloc", "void *(size_t)"),
    library("calloc", "void *(size_t, size_t)"),
    library("realloc", "void *(void *, size_t)"),
    library("free", "void (void *)"),
    library("alloca", "void *(size_t)"),
    library("memcpy", "void *(void *, const void *, size_t)"),
    library("memmove", "void *(void *, const void *, size_t)"),
    library("mempcpy", "void *(void *, const void *, size_t)"),
    library("memset", "void *(void *, int, size_t)"),
    library("memcmp", "int (const void *, const void *, size_t)"),
    library("memchr", "void *(const void *, int, size_t)"),
    library("bzero", "void (void *, size_t)"),
    library("bcmp", "int (const void *, const void *, size_t)"),
    library("bcopy", "void (const void *, void *, size_t)"),
    library("strlen", "size_t (const char *)"),
    library("strnlen", "size_t (const char *, size_t)"),
    library("strcpy", "char *(char *, const char *)"),
    library("strncpy", "char *(char *, const char *, size_t)"),
    library("stpcpy", "char *(char *, const char *)"),
    library("strcat", "char *(char *, const char *)"),
    library("strncat", "char *(char *, const char *, size_t)"),
    library("strcmp", "int (const char *, const char *)"),
    library("strncmp", "int (const char *, const char *, size_t)"),
    library("strcasecmp", "int (const char *, const char *)"),
    library("strncasecmp", "int (const char *, const char *, size_t)"),
    library("strchr", "char *(const char *, int)"),
    library("strrchr", "char *(const char *, int)"),
    library("index", "char *(const char *, int)"),
    library("rindex", "char *(const char *, int)"),
    library("strstr", "char *(const char *, const char *)"),
    library("strpbrk", "char *(const char *, const char *)"),
    library("strspn", "size_t (const char *, const char *)"),
    library("strcspn", "size_t (const char *, const char *)"),
    library("strdup", "char *(const char *)"),
    library("strndup", "char *(const char *, size_t)"),
    // Input and output.
    library("printf", "int (const char *, ...)"),
    library("sprintf", "int (char *, const char *, ...)"),
    library("snprintf", "int (char *, size_t, const char *, ...)"),
    library("fprintf", "int (FILE *, const char *, ...)"),
    library("vprintf", "int (const char *, va_list)"),
    library("vsprintf", "int (char *, const char *, va_list)"),
    library("vsnprintf", "int (char *, size_t, const char *, va_list)"),
    library("vfprintf", "int (FILE *, const char *, va_list)"),
    library("puts", "int (const char *)"),
    library("putchar", "int (int)"),
    library("fputs", "int (const char *, FILE *)"),
    library("fputc", "int (int, FILE *)"),
    library("putc", "int (int, FILE *)"),
    library("fwrite", "size_t (const void *, size_t, size_t, FILE *)"),
    library("scanf", "int (const char *, ...)"),
    library("sscanf", "int (const char *, const char *, ...)"),
    library("fscanf", "int (FILE *, const char *, ...)"),
    // Program termination and integers.
    library("abort", "void (void)", true),
    library("exit", "void (int)", true),
    library("_exit", "void (int)", true),
    library("_Exit", "void (int)", true),
    library("abs", "int (int)"),
    library("labs", "long (long)"),
    library("llabs", "long long (long long)"),
    library("ffs", "int (int)"),
    builtin("ffsl", "int (long)"),
    builtin("ffsll", "int (long long)"),
    // <ctype.h>.
    library("isalnum", "int (int)"),
    library("isalpha", "int (int)"),
    library("isascii", "int (int)"),
    library("isblank", "int (int)"),
    library("iscntrl", "int (int)"),
    library("isdigit", "int (int)"),
    library("isgraph", "int (int)"),
    library("islower", "int (int)"),
    library("isprint", "int (int)"),
    library("ispunct", "int (int)"),
    library("isspace", "int (int)"),
    library("isupper", "int (int)"),
    library("isxdigit", "int (int)"),
    library("toascii", "int (int)"),
    library("tolower", "int (int)"),
    library("toupper", "int (int)"),
    // <math.h>, each also with the suffixes f and l.
    math("fabs", "double (double)", Origin::Library),
    math("sqrt", "double (double)", Origin::Library),
    math("cbrt", "double (double)", Origin::Library),
    math("floor", "double (double)", Origin::Library),
    math("ceil", "double (double)", Origin::Library),
    math("round", "double (double)", Origin::Library),
    math("trunc", "double (double)", Origin::Library),
    math("rint", "double (double)", Origin::Library),
    math("nearbyint", "double (double)", Origin::Library),
    math("exp", "double (double)", Origin::Library),
    math("exp2", "double (double)", Origin::Library),
    math("log", "double (double)", Origin::Library),
    math("log2", "double (double)", Origin::Library),
    math("log10", "double (double)", Origin::Library),
    math("sin", "double (double)", Origin::Library),
    math("cos", "double (double)", Origin::Library),
    math("tan", "double (double)", Origin::Library),
    math("asin", "double (double)", Origin::Library),
    math("acos", "double (double)", Origin::Library),
    math("atan", "double (double)", Origin::Library),
    math("sinh", "double (double)", Origin::Library),
    math("cosh", "double (double)", Origin::Library),
    math("tanh", "double (double)", Origin::Library),
    math("pow", "double (double, double)", Origin::Library),
    math("fmod", "double (double, double)", Origin::Library),
    math("atan2", "double (double, double)", Origin::Library),
    math("copysign", "double (double, double)", Origin::Library),
    math("fmin", "double (double, double)", Origin::Library),
    math("fmax", "double (double, double)", Origin::Library),
    math("fdim", "double (double, double)", Origin::Library),
    math("hypot", "double (double, double)", Origin::Library),
    math("remainder", "double (double, double)", Origin::Library),
    math("nextafter", "double (double, double)", Origin::Library),
    math("ldexp", "double (double, int)", Origin::Library),
    math("frexp", "double (double, int *)", Origin::Library),
    math("modf", "double (double, double *)", Origin::Library),
    math("lround", "long (double)", Origin::Library),
    math("llround", "long long (double)", Origin::Library),
    math("lrint", "long (double)", Origin::Library),
    math("llrint", "long long (double)", Origin::Library),
    math("nan", "double (const char *)", Origin::Library),
    math("inf", "double (void)", Origin::Builtin),
    math("huge_val", "double (void)", Origin::Builtin),
    math("acosh", "double (double)", Origin::Library),
    math("asinh", "double (double)", Origin::Library),
    math("atanh", "double (double)", Origin::Library),
    math("expm1", "double (double)", Origin::Library),
    math("log1p", "double (double)", Origin::Library),
    math("logb", "double (double)", Origin::Library),
    math("erf", "double (double)", Origin::Library),
    math("erfc", "double (double)", Origin::Library),
    math("lgamma", "double (double)", Origin::Library),
    math("tgamma", "double (double)", Origin::Library),
    math("ilogb", "int (double)", Origin::Library),
    math("scalbn", "double (double, int)", Origin::Library),
    math("scalbln", "double (double, long)", Origin::Library),
    math("fma", "double (double, double, double)", Origin::Library),
    math("remquo", "double (double, double, int *)", Origin::Library),
    math("finite", "int (double)", Origin::Library),
    // Type-generic floating-point classification and comparison, and their variants for one type.
    generic("isnan", GenericBuiltin::FloatClassify, Origin::Library),
    generic("isinf", GenericBuiltin::FloatClassify, Origin::Library),
    generic("isfinite", GenericBuiltin::FloatClassify, Origin::Library),
    generic("isnormal", GenericBuiltin::FloatClassify, Origin::Library),
    generic("signbit", GenericBuiltin::FloatClassify, Origin::Library),
    generic("isinf_sign", GenericBuiltin::FloatClassify, Origin::Builtin),
    generic("fpclassify", GenericBuiltin::Fpclassify, Origin::Builtin),
    library("isnanf", "int (float)"),
    library("isnanl", "int (long double)"),
    library("isinff", "int (float)"),
    library("isinfl", "int (long double)"),
    builtin("signbitf", "int (float)"),
    builtin("signbitl", "int (long double)"),
    generic("isgreater", GenericBuiltin::FloatCompare, Origin::Library),
    generic("isgreaterequal", GenericBuiltin::FloatCompare, Origin::Library),
    generic("isless", GenericBuiltin::FloatCompare, Origin::Library),
    generic("islessequal", GenericBuiltin::FloatCompare, Origin::Library),
    generic("islessgreater", GenericBuiltin::FloatCompare, Origin::Library),
    generic("isunordered", GenericBuiltin::FloatCompare, Origin::Library),
    // GCC builtins.
    builtin("expect", "long (long, long)"),
    builtin("expect_with_probability", "long (long, long, double)"),
    builtin("unreachable", "void (void)", true),
    builtin("trap", "void (void)", true),
    builtin("assume_aligned", "void *(const void *, size_t, ...)"),
    builtin("prefetch", "void (const void *, ...)"),
    builtin("object_size", "size_t (const void *, int)"),
    builtin("dynamic_object_size", "size_t (const void *, int)"),
    builtin("frame_address", "void *(unsigned int)"),
    builtin("return_address", "void *(unsigned int)"),
    builtin("bswap16", "uint16 (uint16)"),
    builtin("bswap32", "uint32 (uint32)"),
    builtin("bswap64", "uint64 (uint64)"),
    builtin("clz", "int (unsigned int)"),
    builtin("clzl", "int (unsigned long)"),
    builtin("clzll", "int (unsigned long long)"),
    builtin("ctz", "int (unsigned int)"),
    builtin("ctzl", "int (unsigned long)"),
    builtin("ctzll", "int (unsigned long long)"),
    builtin("clrsb", "int (int)"),
    builtin("clrsbl", "int (long)"),
    builtin("clrsbll", "int (long long)"),
    builtin("popcount", "int (unsigned int)"),
    builtin("popcountl", "int (unsigned long)"),
    builtin("popcountll", "int (unsigned long long)"),
    builtin("parity", "int (unsigned int)"),
    builtin("parityl", "int (unsigned long)"),
    builtin("parityll", "int (unsigned long long)"),
    builtin("sadd_overflow", "_Bool (int, int, int *)"),
    builtin("saddl_overflow", "_Bool (long, long, long *)"),
    builtin("saddll_overflow", "_Bool (long long, long long, long long *)"),
    builtin("ssub_overflow", "_Bool (int, int, int *)"),
    builtin("ssubl_overflow", "_Bool (long, long, long *)"),
    builtin("ssubll_overflow", "_Bool (long long, long long, long long *)"),
    builtin("smul_overflow", "_Bool (int, int, int *)"),
    builtin("smull_overflow", "_Bool (long, long, long *)"),
    builtin("smulll_overflow", "_Bool (long long, long long, long long *)"),
    builtin("uadd_overflow", "_Bool (unsigned int, unsigned int, unsigned int *)"),
    builtin("uaddl_overflow", "_Bool (unsigned long, unsigned long, unsigned long *)"),
    builtin("uaddll_overflow", "_Bool (unsigned long long, unsigned long long, unsigned long long *)"),
    builtin("usub_overflow", "_Bool (unsigned int, unsigned int, unsigned int *)"),
    builtin("usubl_overflow", "_Bool (unsigned long, unsigned long, unsigned long *)"),
    builtin("usubll_overflow", "_Bool (unsigned long long, unsigned long long, unsigned long long *)"),
    builtin("umul_overflow", "_Bool (unsigned int, unsigned int, unsigned int *)"),
    builtin("umull_overflow", "_Bool (unsigned long, unsigned long, unsigned long *)"),
    builtin("umulll_overflow", "_Bool (unsigned long long, unsigned long long, unsigned long long *)"),
    generic("add_overflow", GenericBuiltin::Overflow, Origin::Builtin),
    generic("sub_overflow", GenericBuiltin::Overflow, Origin::Builtin),
    generic("mul_overflow", GenericBuiltin::Overflow, Origin::Builtin),
    generic("constant_p", GenericBuiltin::ConstantP, Origin::Builtin),
    generic("va_start", GenericBuiltin::VaStart, Origin::Builtin),
    generic("va_end", GenericBuiltin::VaEnd, Origin::Builtin),
    generic("va_copy", GenericBuiltin::VaCopy, Origin::Builtin),
    // The checking functions of _FORTIFY_SOURCE, as glibc's headers call them (`__builtin___memcpy_chk`).
    builtin("__memcpy_chk", "void *(void *, const void *, size_t, size_t)"),
    builtin("__mempcpy_chk", "void *(void *, const void *, size_t, size_t)"),
    builtin("__memmove_chk", "void *(void *, const void *, size_t, size_t)"),
    builtin("__memset_chk", "void *(void *, int, size_t, size_t)"),
    builtin("__strcpy_chk", "char *(char *, const char *, size_t)"),
    builtin("__stpcpy_chk", "char *(char *, const char *, size_t)"),
    builtin("__strncpy_chk", "char *(char *, const char *, size_t, size_t)"),
    builtin("__stpncpy_chk", "char *(char *, const char *, size_t, size_t)"),
    builtin("__strcat_chk", "char *(char *, const char *, size_t)"),
    builtin("__strncat_chk", "char *(char *, const char *, size_t, size_t)"),
    builtin("__sprintf_chk", "int (char *, int, size_t, const char *, ...)"),
    builtin("__snprintf_chk", "int (char *, size_t, int, size_t, const char *, ...)"),
    builtin("__vsprintf_chk", "int (char *, int, size_t, const char *, va_list)"),
    builtin("__vsnprintf_chk", "int (char *, size_t, int, size_t, const char *, va_list)"),
    builtin("__printf_chk", "int (int, const char *, ...)"),
    builtin("__fprintf_chk", "int (FILE *, int, const char *, ...)"),
    builtin("__vprintf_chk", "int (int, const char *, va_list)"),
    builtin("__vfprintf_chk", "int (FILE *, int, const char *, va_list)"),
    // The atomic builtins that are not type-generic (the others are in atomicShapes).
    gnu("__atomic_thread_fence", "void (int)"),
    gnu("__atomic_signal_fence", "void (int)"),
    gnu("__atomic_test_and_set", "_Bool (volatile void *, int)"),
    gnu("__atomic_clear", "void (volatile void *, int)"),
    gnu("__atomic_always_lock_free", "_Bool (size_t, const volatile void *)"),
    gnu("__atomic_is_lock_free", "_Bool (size_t, const volatile void *)"),
    gnu("__sync_synchronize", "void (void)"),
};

using AtomicForm = AtomicSignature::Form;
using AtomicResult = AtomicSignature::Result;

struct AtomicShape {
    std::string_view name;
    AtomicSignature signature;
};

// GCC's type-generic atomic builtins ("Built-in Functions for Memory Model Aware Atomic Operations" and "Legacy __sync
// Built-in Functions" in the GCC manual); the parameters are explained at AtomicSignature.
constexpr AtomicShape atomicShapes[] = {
    {"__atomic_load_n", {"Po", AtomicForm::Sized, AtomicResult::Operand}},
    {"__atomic_load", {"Ppo", AtomicForm::Generic, AtomicResult::Void}},
    {"__atomic_store_n", {"Pvo", AtomicForm::Sized, AtomicResult::Void}},
    {"__atomic_store", {"Ppo", AtomicForm::Generic, AtomicResult::Void}},
    {"__atomic_exchange_n", {"Pvo", AtomicForm::Sized, AtomicResult::Operand}},
    {"__atomic_exchange", {"Pppo", AtomicForm::Generic, AtomicResult::Void}},
    {"__atomic_compare_exchange_n", {"Ppvboo", AtomicForm::Sized, AtomicResult::Bool}},
    {"__atomic_compare_exchange", {"Pppboo", AtomicForm::Generic, AtomicResult::Bool}},
    {"__atomic_add_fetch", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_sub_fetch", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_and_fetch", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_xor_fetch", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_or_fetch", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_nand_fetch", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_fetch_add", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_fetch_sub", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_fetch_and", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_fetch_xor", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_fetch_or", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__atomic_fetch_nand", {"Pvo", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_fetch_and_add", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_fetch_and_sub", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_fetch_and_or", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_fetch_and_and", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_fetch_and_xor", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_fetch_and_nand", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_add_and_fetch", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_sub_and_fetch", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_or_and_fetch", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_and_and_fetch", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_xor_and_fetch", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_nand_and_fetch", {"Pv.", AtomicForm::Arithmetic, AtomicResult::Operand}},
    {"__sync_bool_compare_and_swap", {"Pvv.", AtomicForm::Sized, AtomicResult::Bool}},
    {"__sync_val_compare_and_swap", {"Pvv.", AtomicForm::Sized, AtomicResult::Operand}},
    {"__sync_lock_test_and_set", {"Pv.", AtomicForm::Sized, AtomicResult::Operand}},
    {"__sync_lock_release", {"P.", AtomicForm::Sized, AtomicResult::Void}},
};

// A parser for the signatures of the table; they are trusted, so a malformed one is a bug.
class SignatureParser {
public:
    SignatureParser(ast::AstContext& context, std::string_view text) : context_(context), text_(text) {}

    ast::QualType function() {
        const ast::QualType returnType = type();
        expect('(');
        std::vector<ast::QualType> parameters;
        bool isVariadic = false;
        while (!consume(')')) {
            if (consume('.')) {
                expect('.');
                expect('.');
                isVariadic = true;
                continue;
            }
            const ast::QualType parameter = type();
            if (!(builtinKindOf(parameter) == BuiltinKind::Void && parameters.empty())) {
                parameters.push_back(parameter);
            }
            (void)consume(',');
        }
        return context_.functionType(returnType, parameters, isVariadic, true);
    }

private:
    static std::optional<BuiltinKind> builtinKindOf(ast::QualType type) {
        if (const auto* builtin = ast::dyn_cast<ast::BuiltinType>(type.type())) {
            return builtin->builtinKind();
        }
        return std::nullopt;
    }

    void skipSpaces() {
        while (pos_ < text_.size() && text_[pos_] == ' ') {
            ++pos_;
        }
    }

    bool consume(char c) {
        skipSpaces();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char c) { NITWIT_ASSERT(consume(c), "malformed builtin signature"); }

    std::string_view word() {
        skipSpaces();
        const std::size_t start = pos_;
        while (pos_ < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_')) {
            ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }

    ast::QualType type() {
        ast::Qualifiers qualifiers;
        std::string base;
        while (true) {
            const std::size_t mark = pos_;
            const std::string_view next = word();
            if (next.empty()) {
                pos_ = mark;
                break;
            }
            if (next == "const") {
                qualifiers.isConst = true;
            } else if (next == "volatile") {
                qualifiers.isVolatile = true;
            } else {
                base += base.empty() ? "" : " ";
                base += next;
            }
        }
        ast::QualType result = baseType(base).withQualifiers(qualifiers);
        while (consume('*')) {
            result = context_.pointerTo(result);
        }
        return result;
    }

    ast::QualType baseType(std::string_view name) {
        const bool lp64 = context_.target().dataModel() == DataModel::LP64;
        static constexpr std::pair<std::string_view, BuiltinKind> names[] = {
            {"void", BuiltinKind::Void},
            {"FILE", BuiltinKind::Void},
            {"_Bool", BuiltinKind::Bool},
            {"char", BuiltinKind::Char},
            {"int", BuiltinKind::Int},
            {"unsigned int", BuiltinKind::UnsignedInt},
            {"long", BuiltinKind::Long},
            {"unsigned long", BuiltinKind::UnsignedLong},
            {"long long", BuiltinKind::LongLong},
            {"unsigned long long", BuiltinKind::UnsignedLongLong},
            {"float", BuiltinKind::Float},
            {"double", BuiltinKind::Double},
            {"long double", BuiltinKind::LongDouble},
            {"va_list", BuiltinKind::VaList},
            {"uint16", BuiltinKind::UnsignedShort},
            {"uint32", BuiltinKind::UnsignedInt},
        };
        if (name == "size_t") {
            return sizeType(context_);
        }
        if (name == "uint64") {
            return context_.builtin(lp64 ? BuiltinKind::UnsignedLong : BuiltinKind::UnsignedLongLong);
        }
        const auto* found = std::ranges::find(names, name, &std::pair<std::string_view, BuiltinKind>::first);
        NITWIT_ASSERT(found != std::end(names), "unknown type in a builtin signature");
        return context_.builtin(found->second);
    }

    ast::AstContext& context_;
    std::string_view text_;
    std::size_t pos_ = 0;
};

std::optional<BuiltinInfo> infoFor(ast::AstContext& context, const Entry& entry, std::string_view variant) {
    BuiltinInfo info{.generic = entry.generic,
                     .signature = {},
                     .isLibraryFunction = entry.origin == Origin::Library,
                     .isNoreturn = entry.isNoreturn};
    if (entry.generic == GenericBuiltin::None) {
        std::string signature(entry.signature);
        if (!variant.empty()) {
            const std::string replacement = variant == "f" ? "float" : "long double";
            for (std::size_t at = signature.find("double"); at != std::string::npos;
                 at = signature.find("double", at + replacement.size())) {
                signature.replace(at, 6, replacement);
            }
        }
        info.signature = SignatureParser(context, signature).function();
    }
    return info;
}

} // namespace

std::optional<BuiltinInfo> lookupBuiltin(ast::AstContext& context, std::string_view name) {
    if (atomicSignature(name)) {
        return BuiltinInfo{
            .generic = GenericBuiltin::Atomic, .signature = {}, .isLibraryFunction = false, .isNoreturn = false};
    }
    constexpr std::string_view prefix = "__builtin_";
    const bool isBuiltinName = name.starts_with(prefix);
    const std::string_view base = isBuiltinName ? name.substr(prefix.size()) : name;
    const auto matches = [&](const Entry& entry) {
        switch (entry.origin) {
        case Origin::Library:
            return true;
        case Origin::Builtin:
            return isBuiltinName;
        case Origin::Gnu:
            return !isBuiltinName;
        }
        return false;
    };
    for (const Entry& entry : entries) {
        if (entry.name == base && matches(entry)) {
            return infoFor(context, entry, "");
        }
    }
    if (base.size() > 1 && (base.ends_with('f') || base.ends_with('l'))) {
        const std::string_view stem = base.substr(0, base.size() - 1);
        for (const Entry& entry : entries) {
            if (entry.name == stem && entry.hasMathVariants && matches(entry)) {
                return infoFor(context, entry, base.substr(base.size() - 1));
            }
        }
    }
    return std::nullopt;
}

std::optional<AtomicSignature> atomicSignature(std::string_view name) {
    const auto* found = std::ranges::find(atomicShapes, name, &AtomicShape::name);
    if (found == std::end(atomicShapes)) {
        return std::nullopt;
    }
    return found->signature;
}

// GCC's resolve_overloaded_builtin (c-common.cc): T is the unqualified type the first argument points to. Value
// arguments are converted to T as by a cast, without diagnostics, as GCC does; for arithmetic on a pointer, to the
// unsigned integer type of its size instead, since GCC adds bytes, not elements. The interpreter does the operations
// on memory, single-threaded; the memory orders do not matter there.
void Sema::atomicCall(ast::CallExpr* call, std::string_view name) {
    const AtomicSignature signature = *atomicSignature(name);
    const std::span<ast::Expr*> arguments = call->arguments();
    const bool isVariadic = signature.parameters.ends_with('.');
    const std::size_t fixed = signature.parameters.size() - (isVariadic ? 1 : 0);
    if (arguments.size() < fixed) {
        error(call->rightParenLocation(), std::format("too few arguments to function '{}'", name));
    }
    if (arguments.size() > fixed && !isVariadic) {
        error(arguments[fixed]->location(), std::format("too many arguments to function '{}'", name));
    }
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        expr(arguments[i]);
        arguments[i] = rvalue(arguments[i]);
    }

    const ast::QualType objectPointer = arguments[0]->type();
    const bool isObjectPointer = classify(context_, objectPointer) == TypeClass::Pointer
                                 && classify(context_, pointeeOf(objectPointer)) != TypeClass::Void;
    const bool isGeneric = signature.form == AtomicSignature::Form::Generic;
    if (isGeneric && !isObjectPointer) {
        error(arguments[0]->location(), std::format("argument 1 of '{}' must be a non-void pointer type", name));
    }
    const ast::QualType operand = isObjectPointer ? unqualified(context_, pointeeOf(objectPointer)) : ast::QualType();
    const TypeClass operandClass = isObjectPointer ? classify(context_, operand) : TypeClass::Void;
    if (isGeneric) {
        if (operandClass == TypeClass::Function || !isComplete(operand) || isVariablySized(operand)
            || sizeOf(context_, operand) == 0) {
            error(arguments[0]->location(),
                  std::format("argument 1 of '{}' must be a pointer to a nonzero size object", name));
        }
    } else {
        const std::uint64_t size = isObjectPointer && isComplete(operand) ? sizeOf(context_, operand) : 0;
        const bool isSizedScalar =
            (operandClass == TypeClass::Integer || operandClass == TypeClass::Pointer
             || (operandClass == TypeClass::Bool && signature.form != AtomicSignature::Form::Arithmetic))
            && (size == 1 || size == 2 || size == 4 || size == 8 || size == 16);
        if (!isSizedScalar) {
            error(arguments[0]->location(), std::format("operand type '{}' is incompatible with argument 1 of '{}'",
                                                        typeName(objectPointer), name));
        }
    }
    const bool isLoad = name == "__atomic_load_n" || name == "__atomic_load";
    if (!isLoad && isConstQualified(context_, pointeeOf(objectPointer))) {
        warning(arguments[0]->location(),
                std::format("passing argument 1 of '{}' discards 'const' qualifier from pointer target type", name));
    }

    const ast::QualType valueTarget =
        signature.form == AtomicSignature::Form::Arithmetic && operandClass == TypeClass::Pointer
            ? integerTypeForSize(context_, context_.target().pointerBits() / 8, false)
            : operand;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        ast::Expr*& argument = arguments[i];
        const char parameter = i < fixed ? signature.parameters[i] : '.';
        const TypeClass argumentClass = classify(context_, argument->type());
        switch (parameter) {
        case 'p':
            if (argumentClass != TypeClass::Pointer) {
                error(argument->location(), std::format("argument {} of '{}' must be a pointer type", i + 1, name));
            }
            if (signature.form == AtomicSignature::Form::Generic) {
                const ast::QualType pointee = pointeeOf(argument->type());
                if (!isComplete(pointee) || isVariablySized(pointee)
                    || sizeOf(context_, pointee) != sizeOf(context_, operand)) {
                    error(argument->location(), std::format("size mismatch in argument {} of '{}'", i + 1, name));
                }
            }
            break;
        case 'v': {
            const TypeClass targetClass = classify(context_, valueTarget);
            if (argumentClass == TypeClass::Record) {
                error(argument->location(),
                      std::format("aggregate value used where {} was expected",
                                  targetClass == TypeClass::Pointer ? "a pointer" : "an integer"));
            }
            if (!isScalarClass(argumentClass) || argumentClass == TypeClass::Complex
                || (targetClass == TypeClass::Pointer && argumentClass == TypeClass::Floating)) {
                error(argument->location(), std::format("incompatible type for argument {} of '{}'", i + 1, name));
            }
            argument = convert(argument, valueTarget);
            break;
        }
        case 'b':
            if (!isScalarClass(argumentClass)) {
                error(argument->location(), std::format("incompatible type for argument {} of '{}'", i + 1, name));
            }
            argument = convert(argument, context_.builtin(BuiltinKind::Bool));
            break;
        case 'o':
            // The _n forms convert a memory order like an argument of type int, the generic forms want an integer.
            if (isGeneric ? !isIntegerClass(argumentClass) : !isArithmeticClass(argumentClass)) {
                error(argument->location(), std::format("non-integer memory model argument {} of '{}'", i + 1, name));
            }
            argument = convert(argument, context_.builtin(BuiltinKind::Int));
            break;
        default:
            break; // the variables of a __sync builtin
        }
    }

    switch (signature.result) {
    case AtomicSignature::Result::Operand:
        return call->setType(operand, ast::ValueCategory::RValue);
    case AtomicSignature::Result::Void:
        return call->setType(context_.builtin(BuiltinKind::Void), ast::ValueCategory::RValue);
    case AtomicSignature::Result::Bool:
        return call->setType(context_.builtin(BuiltinKind::Bool), ast::ValueCategory::RValue);
    }
}

} // namespace nitwit::sema::detail

namespace nitwit::sema {

std::vector<std::string> builtinFunctionNames() {
    std::vector<std::string> names;
    for (const detail::Entry& entry : detail::entries) {
        std::vector<std::string> variants{std::string(entry.name)};
        if (entry.hasMathVariants) {
            variants.push_back(std::string(entry.name) + "f");
            variants.push_back(std::string(entry.name) + "l");
        }
        for (const std::string& variant : variants) {
            if (entry.origin != detail::Origin::Gnu) {
                names.push_back("__builtin_" + variant);
            }
            if (entry.origin != detail::Origin::Builtin) {
                names.push_back(variant);
            }
        }
    }
    for (const detail::AtomicShape& shape : detail::atomicShapes) {
        names.emplace_back(shape.name);
    }
    return names;
}

} // namespace nitwit::sema
