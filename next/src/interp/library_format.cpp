#include "interp/library_format.hpp"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "support/assert.hpp"

namespace nitwit::interp::library {

namespace {

/// The largest field width and numeric precision the interpreter formats, and the largest output of one call. Larger
/// ones are unsupported, so that a program cannot make the host allocate gigabytes. The precision of `%s` only bounds
/// how much is read, so it is not limited.
constexpr std::int64_t maxField = 1 << 20;
constexpr std::size_t maxOutput = std::size_t{64} << 20;

enum class Length : std::uint8_t { None, Char, Short, Long, LongLong, IntMax, Size, PtrDiff, LongDouble };

/// A conversion specification (C17 7.21.6.1p4).
struct Specification {
    std::string_view text; // as written, for messages
    std::string flags;
    std::optional<int> width; // from the format or an argument
    std::optional<int> precision;
    Length length = Length::None;
    char conversion = 0;
};

/// The host's snprintf, called through a pointer: the format is assembled at run time from a parsed specification,
/// which -Wformat-nonliteral cannot check.
int (*const hostFormat)(char* buffer, std::size_t size, const char* format, ...) = std::snprintf;

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isInteger(ScalarType type) {
    return type != ScalarType::Float && type != ScalarType::Double && type != ScalarType::LongDouble
           && type != ScalarType::Pointer;
}

std::string_view typeName(ScalarType type) {
    switch (type) {
    case ScalarType::Bool:
        return "_Bool";
    case ScalarType::Char:
        return "char";
    case ScalarType::SignedChar:
        return "signed char";
    case ScalarType::UnsignedChar:
        return "unsigned char";
    case ScalarType::Short:
        return "short";
    case ScalarType::UnsignedShort:
        return "unsigned short";
    case ScalarType::Int:
        return "int";
    case ScalarType::UnsignedInt:
        return "unsigned int";
    case ScalarType::Long:
        return "long";
    case ScalarType::UnsignedLong:
        return "unsigned long";
    case ScalarType::LongLong:
        return "long long";
    case ScalarType::UnsignedLongLong:
        return "unsigned long long";
    case ScalarType::Int128:
        return "__int128";
    case ScalarType::UnsignedInt128:
        return "unsigned __int128";
    case ScalarType::Float:
        return "float";
    case ScalarType::Double:
        return "double";
    case ScalarType::LongDouble:
        return "long double";
    case ScalarType::Pointer:
        return "a pointer";
    }
    NITWIT_UNREACHABLE("unknown scalar type");
}

/// Whether digits and a '$' start at `position`: a positional argument (%1$d, %*2$d).
bool isPositional(std::string_view format, std::size_t position) {
    std::size_t end = position;
    while (end < format.size() && isDigit(format[end])) {
        ++end;
    }
    return end > position && end < format.size() && format[end] == '$';
}

class Formatter {
public:
    Formatter(const Call& call, std::size_t firstArgument) : call_(call), next_(firstArgument) {}

    void run(std::string_view format) {
        std::size_t position = 0;
        while (position < format.size()) {
            const std::size_t percent = format.find('%', position);
            result_.text.append(format.substr(position, percent - position));
            if (percent == std::string_view::npos) {
                break;
            }
            position = percent + 1;
            convert(parse(format, position));
            if (result_.text.size() > maxOutput) {
                call_.unsupported(std::format("the output of {} is larger than {} bytes", call_.name(), maxOutput));
            }
        }
    }

    Formatted& result() { return result_; }

private:
    /// Parses the specification after the '%' at position - 1 and leaves `position` after it. Reads the arguments
    /// for `*` widths and precisions, which come before the value (C17 7.21.6.1p5).
    Specification parse(std::string_view format, std::size_t& position) {
        const std::size_t start = position - 1;
        Specification specification;
        // The text up to the conversion character, for messages about the arguments of `*`.
        const std::size_t end = format.find_first_not_of("-+ #0'123456789.*$hlqLjzZt", position);
        specification.text = format.substr(start, end == std::string_view::npos ? end : end + 1 - start);
        const auto at = [&] {
            return position < format.size() ? format[position] : '\0';
        };
        const auto rejectPositional = [&] {
            if (isPositional(format, position)) {
                call_.unsupported(std::format("{}: positional arguments (%n$) are not supported", call_.name()));
            }
        };
        const auto number = [&] {
            std::int64_t value = 0;
            for (; isDigit(at()); ++position) {
                value = value * 10 + (at() - '0');
                if (value > INT_MAX) {
                    call_.unsupported(std::format("{}: a field width or precision exceeds INT_MAX", call_.name()));
                }
            }
            return static_cast<int>(value);
        };
        const auto starArgument = [&] {
            ++position;
            rejectPositional();
            return fieldArgument(specification);
        };
        rejectPositional();
        for (; at() != '\0' && std::string_view("-+ #0'").contains(at()); ++position) {
            specification.flags.push_back(at());
        }
        if (at() == '*') {
            specification.width = starArgument();
        } else if (isDigit(at())) {
            specification.width = number();
        }
        if (specification.width && (*specification.width > maxField || *specification.width < -maxField)) {
            call_.unsupported(std::format("{}: field width {} is too large", call_.name(), *specification.width));
        }
        if (at() == '.') {
            ++position;
            specification.precision = at() == '*' ? starArgument() : number();
        }
        specification.length = parseLength(format, position);
        if (position >= format.size()) {
            call_.undefinedBehavior(
                std::format("the format of {} ends within a conversion specification", call_.name()));
        }
        specification.conversion = format[position++];
        specification.text = format.substr(start, position - start);
        return specification;
    }

    static Length parseLength(std::string_view format, std::size_t& position) {
        const std::string_view rest = format.substr(position);
        for (const auto& [text, length] :
             {std::pair{"hh", Length::Char}, std::pair{"h", Length::Short}, std::pair{"ll", Length::LongLong},
              std::pair{"l", Length::Long}, std::pair{"q", Length::LongLong}, std::pair{"L", Length::LongDouble},
              std::pair{"j", Length::IntMax}, std::pair{"z", Length::Size}, std::pair{"Z", Length::Size},
              std::pair{"t", Length::PtrDiff}}) {
            if (rest.starts_with(text)) {
                position += std::string_view(text).size();
                return length;
            }
        }
        return Length::None;
    }

    /// The index of the next argument; undefined behaviour unless its type fits `expected`: the same type, or for
    /// integers, a type of the same width (C17 7.21.6.1p9; signedness may differ as for va_arg, C17 7.16.1.1p2).
    std::size_t nextArgument(const Specification& specification, ScalarType expected) {
        const std::size_t index = next_++;
        const ScalarType actual = call_.argumentType(index);
        const bool sameWidthInteger = isInteger(expected) && isInteger(actual)
                                      && call_.target().layout(expected).size == call_.target().layout(actual).size;
        if (actual != expected && !sameWidthInteger) {
            call_.undefinedBehavior(std::format("{}: {} expects {}, but argument {} is {}", call_.name(),
                                                specification.text, typeName(expected), index + 1, typeName(actual)));
        }
        return index;
    }

    /// A field width or precision given as `*`, an int.
    int fieldArgument(const Specification& specification) {
        return call_.intArgument(nextArgument(specification, ScalarType::Int));
    }

    [[noreturn]] void invalid(const Specification& specification) const {
        call_.undefinedBehavior(
            std::format("{}: invalid conversion specification {}", call_.name(), specification.text));
    }

    void convert(const Specification& specification) {
        switch (specification.conversion) {
        case 'd':
        case 'i':
            return convertInteger(specification, true);
        case 'o':
        case 'u':
        case 'x':
        case 'X':
            return convertInteger(specification, false);
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            return convertFloat(specification);
        case 'c':
            return convertCharacter(specification);
        case 's':
            return convertString(specification);
        case 'p':
            if (specification.length != Length::None) {
                invalid(specification);
            }
            call_.noteWrittenPointerValue(); // the output, which sprintf writes, shows a pointer argument
            return host(specification, "",
                        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
                            call_.pointerArgument(nextArgument(specification, ScalarType::Pointer)))));
        case '%':
            result_.text.push_back('%'); // glibc ignores flags and width here
            return;
        case 'n':
            call_.unsupported(std::format("{}: %n is not supported", call_.name()));
        case 'm':
        case 'C':
        case 'S':
            call_.unsupported(std::format("{}: {} is not supported", call_.name(), specification.text));
        default:
            invalid(specification);
        }
    }

    /// The precision of a numeric conversion is the number of digits the host produces.
    void limitPrecision(const Specification& specification) const {
        if (specification.precision && *specification.precision > maxField) {
            call_.unsupported(std::format("{}: precision {} is too large", call_.name(), *specification.precision));
        }
    }

    /// The type the argument is converted to before printing (C17 7.21.6.1p7).
    [[nodiscard]] ScalarType integerType(const Specification& specification, bool isSigned) const {
        const bool lp64 = call_.target().dataModel() == DataModel::LP64;
        switch (specification.length) {
        case Length::None:
            return isSigned ? ScalarType::Int : ScalarType::UnsignedInt;
        case Length::Char:
            return isSigned ? ScalarType::SignedChar : ScalarType::UnsignedChar;
        case Length::Short:
            return isSigned ? ScalarType::Short : ScalarType::UnsignedShort;
        case Length::Long:
            return isSigned ? ScalarType::Long : ScalarType::UnsignedLong;
        case Length::LongLong:
        case Length::IntMax:     // intmax_t has 64 bits in both data models
        case Length::LongDouble: // glibc reads %Ld as %lld
            return isSigned ? ScalarType::LongLong : ScalarType::UnsignedLongLong;
        case Length::Size: // size_t; ssize_t for the signed conversions
        case Length::PtrDiff:
            if (lp64) {
                return isSigned ? ScalarType::Long : ScalarType::UnsignedLong;
            }
            return isSigned ? ScalarType::Int : ScalarType::UnsignedInt;
        }
        NITWIT_UNREACHABLE("unknown length modifier");
    }

    void convertInteger(const Specification& specification, bool isSigned) {
        limitPrecision(specification);
        const ScalarType type = integerType(specification, isSigned);
        // A char or short argument arrives promoted to int (C17 7.21.6.1p7).
        const bool promoted = specification.length == Length::Char || specification.length == Length::Short;
        const std::size_t index = nextArgument(specification, promoted ? ScalarType::Int : type);
        const UInt128 bits = call_.integerArgument(index, type);
        // The host formats the converted value as long long, whose range covers every target type.
        if (isSigned) {
            host(specification, "ll", static_cast<long long>(static_cast<Int128>(bits)));
        } else {
            host(specification, "ll", static_cast<unsigned long long>(bits));
        }
    }

    void convertFloat(const Specification& specification) {
        limitPrecision(specification);
        switch (specification.length) {
        case Length::None:
        case Length::Long: { // %lf is %f (C17 7.21.6.1p7)
            const std::size_t index = nextArgument(specification, ScalarType::Double);
            return host(specification, "", call_.floatArgument(index, value::FloatFormat::Double).asDouble());
        }
        case Length::LongDouble:
        case Length::LongLong: { // glibc reads %llf as %Lf
            const std::size_t index = nextArgument(specification, ScalarType::LongDouble);
            return host(specification, "L", call_.floatArgument(index, value::FloatFormat::LongDouble).asLongDouble());
        }
        default:
            invalid(specification);
        }
    }

    void convertCharacter(const Specification& specification) {
        if (specification.length == Length::Long) {
            call_.unsupported(
                std::format("{}: wide characters ({}) are not supported", call_.name(), specification.text));
        }
        if (specification.length != Length::None) {
            invalid(specification);
        }
        // The int argument is converted to unsigned char (C17 7.21.6.1p8).
        const int character = call_.intArgument(nextArgument(specification, ScalarType::Int));
        host(specification, "", static_cast<int>(static_cast<unsigned char>(character)));
    }

    void convertString(const Specification& specification) {
        if (specification.length == Length::Long) {
            call_.unsupported(std::format("{}: wide strings ({}) are not supported", call_.name(), specification.text));
        }
        if (specification.length != Length::None) {
            invalid(specification);
        }
        const Address address = call_.pointerArgument(nextArgument(specification, ScalarType::Pointer));
        const bool limited = specification.precision && *specification.precision >= 0;
        std::string text;
        if (address == 0) {
            // glibc prints "(null)" if the precision admits all of it, and nothing otherwise.
            text = !limited || *specification.precision >= 6 ? "(null)" : "";
        } else {
            // The precision limits how much is read: the array need not be terminated then (C17 7.21.6.1p8).
            const std::uint64_t limit = limited ? static_cast<std::uint64_t>(*specification.precision)
                                                : std::numeric_limits<std::uint64_t>::max();
            text = call_.readString(address, limit);
            const bool readTerminator = text.size() < limit;
            result_.reads.push_back({.address = address, .size = text.size() + (readTerminator ? 1 : 0)});
        }
        host(specification, "", text.c_str());
    }

    /// Formats one conversion with the host's snprintf: the specification with `*` for the width and precision,
    /// `hostLength` as length modifier, and `value` of the host type that goes with it.
    template <typename T>
    void host(const Specification& specification, std::string_view hostLength, T value) {
        const std::string format =
            std::format("%{}{}{}{}{}", specification.flags, specification.width ? "*" : "",
                        specification.precision ? ".*" : "", hostLength, specification.conversion);
        const auto print = [&](char* buffer, std::size_t size) {
            if (specification.width && specification.precision) {
                return hostFormat(buffer, size, format.c_str(), *specification.width, *specification.precision, value);
            }
            if (specification.width) {
                return hostFormat(buffer, size, format.c_str(), *specification.width, value);
            }
            if (specification.precision) {
                return hostFormat(buffer, size, format.c_str(), *specification.precision, value);
            }
            return hostFormat(buffer, size, format.c_str(), value);
        };
        const int length = print(nullptr, 0);
        if (length < 0) {
            call_.unsupported(std::format("{}: the host cannot format {}", call_.name(), specification.text));
        }
        std::string& output = result_.text;
        const std::size_t start = output.size();
        output.resize(start + static_cast<std::size_t>(length) + 1);
        print(output.data() + start, static_cast<std::size_t>(length) + 1);
        output.pop_back(); // the terminating null character
    }

    const Call& call_;
    std::size_t next_;
    Formatted result_;
};

} // namespace

Formatted formatPrintf(const Call& call, Address format, std::size_t firstArgument) {
    const std::string text = call.readString(format);
    Formatter formatter(call, firstArgument);
    formatter.result().reads.push_back({.address = format, .size = text.size() + 1});
    formatter.run(text);
    return std::move(formatter.result());
}

} // namespace nitwit::interp::library
