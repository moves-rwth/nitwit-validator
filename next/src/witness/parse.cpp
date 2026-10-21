#include "witness/parse.hpp"

namespace nitwit::witness {

WitnessFormat detectFormat(std::string_view contents) {
    constexpr std::string_view byteOrderMark = "\xEF\xBB\xBF";
    if (contents.starts_with(byteOrderMark)) {
        contents.remove_prefix(byteOrderMark.size());
    }
    const std::size_t first = contents.find_first_not_of(" \t\r\n");
    return first != std::string_view::npos && contents[first] == '<' ? WitnessFormat::Graphml : WitnessFormat::Yaml;
}

} // namespace nitwit::witness
