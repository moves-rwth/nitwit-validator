#pragma once

// Where waypoints are evaluated: witness locations (a line and an optional column) turned into byte ranges of the
// program file, so that matching an event costs a few comparisons (validate/README.md, "Evaluation points").

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "support/source_manager.hpp"
#include "witness/model.hpp"

namespace nitwit::validate {

/// The program bytes a witness location refers to: the lines with its line number and, if the witness gives one, the
/// column in them. Without a column, every location in the lines matches.
class Place {
public:
    [[nodiscard]] bool contains(SourceLocation location) const;

private:
    friend class LocationMap;

    struct Line {
        std::uint32_t begin = 0; // offset of the first byte
        std::uint32_t end = 0;   // offset of the first byte of the next line
    };

    FileId file_ = FileId::None;
    std::vector<Line> lines_; // empty if the line does not exist
    std::optional<std::uint32_t> column_;
};

/// Maps witness locations to places in the program file. Lines and columns count bytes from 1 (witness format 2.x,
/// "location"). For a preprocessed file, a witness line is a presumed line of the original file, and it may cover
/// several physical lines; lines of other files (headers) never match.
class LocationMap {
public:
    LocationMap(const SourceManager& sources, FileId file, bool isPreprocessed);

    [[nodiscard]] Place place(const witness::Location& location) const;

    /// "line:column" of a program location as the witness counts them, for messages.
    [[nodiscard]] std::string describe(SourceLocation location) const;

private:
    const SourceManager& sources_;
    FileId file_;
    bool isPreprocessed_;
    // Preprocessed files only: the physical lines of each presumed line of the original file.
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> physicalLines_;
};

} // namespace nitwit::validate
