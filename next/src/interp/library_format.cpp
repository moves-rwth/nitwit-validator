#include "interp/library_format.hpp"

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

/// The largest field width and precision the interpreter formats, and the largest output of one call. Larger ones are
/// unsupported, so that a program cannot make the host allocate gigabytes.
constexpr int maxField = 1 << 20;
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

std::string_view kindName(value::Value::Kind kind) {
    switch (kind) {
    case value::Value::Kind::None:
        return "void";
    case value::Value::Kind::Integer:
        return "an integer";
    case value::Value::Kind::Float:
        return "a float";
    case value::Value::Kind::Double:
        return "a double";
    case value::Value::Kind::LongDouble:
        return "a long double";
    case value::Value::Kind::Bytes:
        return "a structure";
    }
    NITWIT_UNREACHABLE("unknown value kind");
}

class Formatter {
public:
    Formatter(const Call& call, std::size_t firstArgument) : call_(call), next_(firstArgument) {}

    std::string run(std::string_view format) {
        std::size_t position = 0;
        while (position < format.size()) {
            const std::size_t percent = format.find('%', position);
            output_.append(format.substr(position, percent - position));
            if (percent == std::string_view::npos) {
                break;
            }
            position = percent + 1;
            convert(parse(format, position));
            if (output_.size() > maxOutput) {
                call_.unsupported(std::format("the output of {} is larger than {} bytes", call_.name(), maxOutput));
            }
        }
        return std::move(output_);
    }

private:
    /// Parses the specification after the '%' at position - 1 and leaves `position` after it. Reads the arguments
    /// for `*` widths and precisions, which come before the value (C17 7.21.6.1p5).
    Specification parse(std::string_view format, std::size_t& position) {
        const std::size_t start = position - 1;
        Specification specification;
        const auto at = [&] {
            return position < format.size() ? format[position] : '\0';
        };
        const auto number = [&] {
            int value = 0;
            for (; isDigit(at()); ++position) {
                value = value * 10 + (at() - '0');
                if (value > maxField) {
                    call_.unsupported(
                        std::format("{}: field width or precision larger than {}", call_.name(), maxField));
                }
            }
            return value;
        };
        std::size_t digitsEnd = position;
        while (digitsEnd < format.size() && isDigit(format[digitsEnd])) {
            ++digitsEnd;
        }
        if (digitsEnd > position && digitsEnd < format.size() && format[digitsEnd] == '$') {
            call_.unsupported(std::format("{}: positional arguments (%n$) are not supported", call_.name()));
        }
        for (; at() != '\0' && std::string_view("-+ #0'").contains(at()); ++position) {
            specification.flags.push_back(at());
        }
        if (at() == '*') {
            ++position;
            specification.width = fieldArgument();
        } else if (isDigit(at())) {
            specification.width = number();
        }
        if (at() == '.') {
            ++position;
            if (at() == '*') {
                ++position;
                specification.precision = fieldArgument();
            } else {
                specification.precision = number();
            }
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

    /// A field width or precision given as `*`.
    int fieldArgument() {
        const int value = call_.intArgument(next_++);
        if (value > maxField || value < -maxField) {
            call_.unsupported(std::format("{}: field width or precision {} is too large", call_.name(), value));
        }
        return value;
    }

    const value::Value& argument(const Specification& specification, value::Value::Kind expected,
                                 std::string_view expectedName) {
        const std::size_t index = next_++;
        const value::Value& value = call_.argument(index);
        const bool promotedFloat = expected == value::Value::Kind::Double && value.kind() == value::Value::Kind::Float;
        if (value.kind() != expected && !promotedFloat) {
            call_.undefinedBehavior(std::format("{}: {} expects {}, but argument {} is {}", call_.name(),
                                                specification.text, expectedName, index + 1, kindName(value.kind())));
        }
        return value;
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
            return host(specification, "",
                        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(call_.pointerArgument(next_++))));
        case '%':
            output_.push_back('%'); // glibc ignores flags and width here
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

    /// The type the argument is converted to before printing (C17 7.21.6.1p7).
    [[nodiscard]] ScalarType integerType(const Specification& specification, bool isSigned) const {
        const TargetInfo& target = call_.target();
        const bool lp64 = target.dataModel() == DataModel::LP64;
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
        argument(specification, value::Value::Kind::Integer, "an integer");
        const UInt128 bits = call_.integerArgument(next_ - 1, integerType(specification, isSigned));
        // The host formats the converted value as long long, whose range covers every target type.
        if (isSigned) {
            host(specification, "ll", static_cast<long long>(static_cast<Int128>(bits)));
        } else {
            host(specification, "ll", static_cast<unsigned long long>(bits));
        }
    }

    void convertFloat(const Specification& specification) {
        switch (specification.length) {
        case Length::None:
        case Length::Long: { // %lf is %f (C17 7.21.6.1p7)
            const value::Value& value = argument(specification, value::Value::Kind::Double, "a double");
            const double number =
                value.kind() == value::Value::Kind::Float ? static_cast<double>(value.asFloat()) : value.asDouble();
            return host(specification, "", number);
        }
        case Length::LongDouble:
        case Length::LongLong: // glibc reads %llf as %Lf
            return host(specification, "L",
                        argument(specification, value::Value::Kind::LongDouble, "a long double").asLongDouble());
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
        argument(specification, value::Value::Kind::Integer, "an int");
        // The int argument is converted to unsigned char (C17 7.21.6.1p8).
        host(specification, "", static_cast<int>(static_cast<unsigned char>(call_.intArgument(next_ - 1))));
    }

    void convertString(const Specification& specification) {
        if (specification.length == Length::Long) {
            call_.unsupported(std::format("{}: wide strings ({}) are not supported", call_.name(), specification.text));
        }
        if (specification.length != Length::None) {
            invalid(specification);
        }
        argument(specification, value::Value::Kind::Integer, "a pointer");
        const Address address = call_.pointerArgument(next_ - 1);
        const bool limited = specification.precision && *specification.precision >= 0;
        std::string text;
        if (address == 0) {
            // glibc prints "(null)" if the precision admits all of it, and nothing otherwise.
            text = !limited || *specification.precision >= 6 ? "(null)" : "";
        } else {
            // The precision limits how much is read: the array need not be terminated then (C17 7.21.6.1p8).
            text = call_.readString(address, limited ? static_cast<std::uint64_t>(*specification.precision)
                                                     : std::numeric_limits<std::uint64_t>::max());
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
        const std::size_t start = output_.size();
        output_.resize(start + static_cast<std::size_t>(length) + 1);
        print(output_.data() + start, static_cast<std::size_t>(length) + 1);
        output_.pop_back(); // the terminating null character
    }

    const Call& call_;
    std::size_t next_;
    std::string output_;
};

} // namespace

std::string formatPrintf(const Call& call, Address format, std::size_t firstArgument) {
    return Formatter(call, firstArgument).run(call.readString(format));
}

} // namespace nitwit::interp::library
