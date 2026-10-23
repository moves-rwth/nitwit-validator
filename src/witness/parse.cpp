#include "witness/parse.hpp"

namespace nitwit::witness {

WitnessFormat detectFormat(std::string_view contents) {
    // pugixml decodes UTF-16 with a byte order mark; no YAML witness in the SV-COMP corpus is UTF-16.
    if (contents.starts_with("\xFF\xFE") || contents.starts_with("\xFE\xFF")) {
        return WitnessFormat::Graphml;
    }
    constexpr std::string_view byteOrderMark = "\xEF\xBB\xBF";
    if (contents.starts_with(byteOrderMark)) {
        contents.remove_prefix(byteOrderMark.size());
    }
    const std::size_t first = contents.find_first_not_of(" \t\r\n");
    return first != std::string_view::npos && contents[first] == '<' ? WitnessFormat::Graphml : WitnessFormat::Yaml;
}

} // namespace nitwit::witness
