#include "validate/location.hpp"

#include <algorithm>
#include <format>

namespace nitwit::validate {

bool Place::contains(SourceLocation location) const {
    if (location.file != file_) {
        return false;
    }
    return std::ranges::any_of(lines_, [&](const Line& line) {
        if (column_) {
            return location.offset == line.begin + *column_ - 1 && location.offset < line.end;
        }
        return line.begin <= location.offset && location.offset < line.end;
    });
}

LocationMap::LocationMap(const SourceManager& sources, FileId file, bool isPreprocessed)
    : sources_(sources), file_(file), isPreprocessed_(isPreprocessed) {
    if (!isPreprocessed_) {
        return;
    }
    // A line marker applies to whole physical lines, so the presumed line of a line's first byte holds for the line.
    const std::string_view originalFile = sources_.name(file_);
    for (std::uint32_t line = 1; line <= sources_.lineCount(file_); ++line) {
        const std::optional<SourceLocation> start = sources_.location(file_, {line, 1});
        const PresumedLocation presumed = sources_.presumed(*start);
        if (presumed.fileName == originalFile) {
            physicalLines_[presumed.line].push_back(line);
        }
    }
}

Place LocationMap::place(const witness::Location& location) const {
    Place place;
    place.file_ = file_;
    place.column_ = location.column;
    std::vector<std::uint32_t> physical;
    if (!isPreprocessed_) {
        if (location.line >= 1 && location.line <= sources_.lineCount(file_)) {
            physical.push_back(location.line);
        }
    } else if (const auto found = physicalLines_.find(location.line); found != physicalLines_.end()) {
        physical = found->second;
    }
    const auto fileSize = static_cast<std::uint32_t>(sources_.contents(file_).size());
    for (const std::uint32_t line : physical) {
        const std::uint32_t begin = sources_.location(file_, {line, 1})->offset;
        const std::uint32_t end =
            line < sources_.lineCount(file_) ? sources_.location(file_, {line + 1, 1})->offset : fileSize;
        place.lines_.push_back({.begin = begin, .end = end});
    }
    return place;
}

std::string LocationMap::describe(SourceLocation location) const {
    if (isPreprocessed_) {
        const PresumedLocation presumed = sources_.presumed(location);
        return std::format("{}:{}", presumed.line, presumed.column);
    }
    const LineColumn position = sources_.lineColumn(location);
    return std::format("{}:{}", position.line, position.column);
}

} // namespace nitwit::validate
