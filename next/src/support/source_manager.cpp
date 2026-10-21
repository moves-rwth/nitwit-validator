#include "support/source_manager.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>

#include "support/assert.hpp"

namespace nitwit {

namespace {

std::vector<std::uint32_t> computeLineStarts(std::string_view contents) {
    std::vector<std::uint32_t> starts{0};
    for (std::size_t i = 0; i < contents.size(); ++i) {
        if (contents[i] == '\n') {
            starts.push_back(static_cast<std::uint32_t>(i + 1));
        }
    }
    return starts;
}

} // namespace

FileId SourceManager::addFile(std::string name, std::string contents) {
    NITWIT_ASSERT(contents.size() < std::numeric_limits<std::uint32_t>::max(), "source files must be below 4 GiB");
    auto lineStarts = computeLineStarts(contents);
    files_.push_back(File{std::move(name), std::move(contents), std::move(lineStarts), {}});
    return static_cast<FileId>(files_.size());
}

std::expected<FileId, Error> SourceManager::loadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::unexpected(Error{std::format("cannot open '{}'", path.string())});
    }
    std::string contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        return std::unexpected(Error{std::format("cannot read '{}'", path.string())});
    }
    if (contents.size() >= std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(Error{std::format("'{}' is too large", path.string())});
    }
    return addFile(path.string(), std::move(contents));
}

const SourceManager::File& SourceManager::file(FileId id) const {
    const auto index = static_cast<std::size_t>(id);
    NITWIT_ASSERT(index >= 1 && index <= files_.size(), "unknown file id");
    return files_[index - 1];
}

std::string_view SourceManager::name(FileId file) const {
    return this->file(file).name;
}

std::string_view SourceManager::contents(FileId file) const {
    return this->file(file).contents;
}

std::uint32_t SourceManager::lineCount(FileId file) const {
    return static_cast<std::uint32_t>(this->file(file).lineStarts.size());
}

LineColumn SourceManager::lineColumn(SourceLocation location) const {
    const File& f = file(location.file);
    NITWIT_ASSERT(location.offset <= f.contents.size(), "location beyond the end of the file");
    // The line is the last one starting at or before the offset.
    const auto next = std::ranges::upper_bound(f.lineStarts, location.offset);
    const auto line = static_cast<std::uint32_t>(next - f.lineStarts.begin());
    return {line, location.offset - *std::prev(next) + 1};
}

std::optional<SourceLocation> SourceManager::location(FileId file, LineColumn position) const {
    const File& f = this->file(file);
    if (position.line == 0 || position.line > f.lineStarts.size() || position.column == 0) {
        return std::nullopt;
    }
    const std::uint32_t lineStart = f.lineStarts[position.line - 1];
    const auto lineLength = static_cast<std::uint32_t>(lineText(file, position.line).size());
    if (position.column - 1 > lineLength) {
        return std::nullopt;
    }
    return SourceLocation{file, lineStart + position.column - 1};
}

std::string_view SourceManager::lineText(FileId file, std::uint32_t line) const {
    const File& f = this->file(file);
    NITWIT_ASSERT(line >= 1 && line <= f.lineStarts.size(), "line out of range");
    const std::uint32_t begin = f.lineStarts[line - 1];
    std::uint32_t end =
        line < f.lineStarts.size() ? f.lineStarts[line] - 1 : static_cast<std::uint32_t>(f.contents.size());
    if (end > begin && f.contents[end - 1] == '\r') {
        --end;
    }
    return std::string_view(f.contents).substr(begin, end - begin);
}

void SourceManager::addLineMarker(FileId file, std::uint32_t physicalLine, std::uint32_t presumedLine,
                                  std::string fileName) {
    File& f = files_[static_cast<std::size_t>(file) - 1];
    NITWIT_ASSERT(f.markers.empty() || f.markers.back().physicalLine < physicalLine,
                  "line markers must be added in increasing order");
    f.markers.push_back(LineMarker{physicalLine, presumedLine, std::move(fileName)});
}

PresumedLocation SourceManager::presumed(SourceLocation location) const {
    const File& f = file(location.file);
    const LineColumn physical = lineColumn(location);
    const auto next = std::ranges::upper_bound(f.markers, physical.line, {}, &LineMarker::physicalLine);
    if (next == f.markers.begin()) {
        return {f.name, physical.line, physical.column};
    }
    const LineMarker& marker = *std::prev(next);
    return {marker.fileName, marker.presumedLine + (physical.line - marker.physicalLine), physical.column};
}

std::string SourceManager::describe(SourceLocation location) const {
    const LineColumn position = lineColumn(location);
    return std::format("{}:{}:{}", name(location.file), position.line, position.column);
}

} // namespace nitwit
