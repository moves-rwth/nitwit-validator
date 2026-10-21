#pragma once

// Source files and positions in them. A position is a byte offset; the source manager maps it to a physical line and
// column, and through line markers (`# 12 "a.c"`) to a presumed location.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/error.hpp"

namespace nitwit {

/// A file registered with a SourceManager. FileId::None identifies no file.
enum class FileId : std::uint32_t { None = 0 };

struct SourceLocation {
    FileId file = FileId::None;
    std::uint32_t offset = 0;

    [[nodiscard]] bool isValid() const { return file != FileId::None; }
    friend bool operator==(const SourceLocation&, const SourceLocation&) = default;
};

/// The half-open byte range [begin, end) in one file.
struct SourceRange {
    SourceLocation begin;
    SourceLocation end;

    friend bool operator==(const SourceRange&, const SourceRange&) = default;
};

/// A 1-based line and column. Columns count bytes, so a tab is one column, as in the witness formats.
struct LineColumn {
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    friend bool operator==(const LineColumn&, const LineColumn&) = default;
};

struct PresumedLocation {
    std::string_view fileName;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

class SourceManager {
public:
    /// Registers a file. The name is used in messages and as the presumed file name before the first line marker.
    FileId addFile(std::string name, std::string contents);

    /// Reads a file and registers it under its path.
    std::expected<FileId, Error> loadFile(const std::filesystem::path& path);

    [[nodiscard]] std::string_view name(FileId file) const;
    [[nodiscard]] std::string_view contents(FileId file) const;
    [[nodiscard]] std::uint32_t lineCount(FileId file) const;

    [[nodiscard]] LineColumn lineColumn(SourceLocation location) const;

    /// The location at a physical line and column. The column may be one past the end of the line.
    [[nodiscard]] std::optional<SourceLocation> location(FileId file, LineColumn position) const;

    /// The text of a physical line, without its line terminator ("\n" or "\r\n").
    [[nodiscard]] std::string_view lineText(FileId file, std::uint32_t line) const;

    /// Records a line marker: physical line `physicalLine` and the lines after it are presumed to be lines
    /// `presumedLine`, `presumedLine + 1`, ... of `fileName`. Markers must be added in increasing physical order.
    void addLineMarker(FileId file, std::uint32_t physicalLine, std::uint32_t presumedLine, std::string fileName);

    /// The location after applying the last line marker before it; the physical location if there is none.
    [[nodiscard]] PresumedLocation presumed(SourceLocation location) const;

    /// "name:line:column" of the physical location, for messages.
    [[nodiscard]] std::string describe(SourceLocation location) const;

private:
    struct LineMarker {
        std::uint32_t physicalLine;
        std::uint32_t presumedLine;
        std::string fileName;
    };

    struct File {
        std::string name;
        std::string contents;
        std::vector<std::uint32_t> lineStarts; // offset of the first byte of each line
        std::vector<LineMarker> markers;
    };

    [[nodiscard]] const File& file(FileId id) const;

    std::vector<File> files_; // FileId n is files_[n - 1]
};

} // namespace nitwit
