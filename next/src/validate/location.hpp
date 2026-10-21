#pragma once

// Where waypoints are evaluated: witness locations (a line and an optional column) turned into positions in the
// program file, so that matching an event costs a few comparisons (validate/README.md, "Evaluation points").

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/decl.hpp"
#include "support/source_manager.hpp"
#include "witness/model.hpp"

namespace nitwit::validate {

/// The program positions where a waypoint is evaluated: byte offsets of the events it matches, or for a target
/// without column, whole lines.
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
    std::vector<std::uint32_t> offsets_;
    std::vector<Line> lines_;
};

/// Maps witness locations to places in the program file. Lines and columns count bytes from 1 (witness format 2.x,
/// "location"). For a preprocessed program, a witness line is a presumed line of the original file, which may cover
/// several physical lines, and a column beyond the end of the original line matches nothing; lines of other files
/// (headers) never match.
///
/// The evaluation points come from the program's AST: statements (assumption), the keywords of conditions and the
/// `?` of conditional expressions (branching), and the `)` of calls (function_enter, function_return).
/// - A waypoint without a column is evaluated at the first evaluation point of its type in the line, as the crafted
///   witnesses of sv-benchmarks (`c/validation-crafted`) expect. A target without a column matches the whole line.
/// - A block has no evaluation point of its own: an assumption at a `{` is evaluated before the first statement in
///   the block, which runs in the same state.
class LocationMap {
public:
    /// `originalFile` is FileId::None unless `file` is the preprocessor's output; `unit` is the program's AST.
    LocationMap(const SourceManager& sources, FileId file, FileId originalFile, const ast::TranslationUnit& unit);

    [[nodiscard]] Place place(const witness::Location& location, witness::WaypointType type) const;

    /// "line:column" of a program location as the witness counts them, for messages.
    [[nodiscard]] std::string describe(SourceLocation location) const;

private:
    struct Point {
        std::uint32_t offset = 0;    // where the construct begins
        std::uint32_t evaluated = 0; // where it is evaluated: different for blocks
    };

    void collectPoints(const ast::FunctionDecl& function);
    /// The physical lines of the file that a witness line refers to, as byte ranges.
    [[nodiscard]] std::vector<Place::Line> linesOf(const witness::Location& location) const;
    [[nodiscard]] const std::vector<Point>& pointsOf(witness::WaypointType type) const;

    const SourceManager& sources_;
    FileId file_;
    FileId originalFile_;
    // Preprocessed programs only: the physical lines of each presumed line of the original file.
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> physicalLines_;
    // The evaluation points of the program, sorted by offset.
    std::vector<Point> statements_;
    std::vector<Point> branchings_;
    std::vector<Point> calls_;
};

} // namespace nitwit::validate
