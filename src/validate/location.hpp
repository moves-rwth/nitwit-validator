#pragma once

// Where waypoints are evaluated: witness locations (a line and an optional column) turned into positions in the
// program file, so that matching an event costs a few comparisons (validate/README.md, "Evaluation points").

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "support/source_manager.hpp"
#include "witness/model.hpp"

namespace nitwit::validate {

/// The program positions where a waypoint is evaluated: byte offsets of the events it matches.
class Place {
public:
    [[nodiscard]] bool contains(SourceLocation location) const;

    [[nodiscard]] FileId file() const { return file_; }
    /// The offsets of the events the place matches; empty for a location without evaluation point of its type.
    [[nodiscard]] std::span<const std::uint32_t> offsets() const { return offsets_; }
    /// For waypoints other than targets: the function containing each evaluation point of offsets().
    [[nodiscard]] std::span<const ast::FunctionDecl* const> functions() const { return functions_; }
    /// Whether the column lies in a part of the line that the preprocessor changed (macro expansion, comments,
    /// collapsed blanks), so that the waypoint matches nothing in that physical line.
    [[nodiscard]] bool isAfterPreprocessorChange() const { return isAfterPreprocessorChange_; }

private:
    friend class LocationMap;

    struct Line {
        std::uint32_t begin = 0; // offset of the first byte
        std::uint32_t end = 0;   // offset of the first byte of the next line
    };

    FileId file_ = FileId::None;
    std::vector<std::uint32_t> offsets_;
    std::vector<const ast::FunctionDecl*> functions_;
    bool isAfterPreprocessorChange_ = false;
};

/// Maps witness locations to places in the program file. Lines and columns count bytes from 1 (witness format 2.x,
/// "location"). For a preprocessed program, a witness line is a presumed line of the original file, which may cover
/// several physical lines; lines of other files (headers) never match. A column there is only a position in a physical
/// line where that line and the original line agree up to and including the column: the preprocessor expands macros,
/// replaces comments by a blank and collapses runs of blanks, which moves the text after them. Blanks agree with blanks
/// (cpp writes a tab of the indentation as a space). A column after a change, or beyond the original line, matches
/// nothing in that physical line.
///
/// The evaluation points come from the program's AST: statements (assumption), the keywords of conditions (for a do
/// loop `do` and `while`) and the `?` of conditional expressions (branching), and the `)` of calls (function_enter,
/// function_return). A column must be one of them; a column inside a construct matches nothing.
/// - A waypoint without a column is evaluated at the first evaluation point of its type in the line, as the crafted
///   witnesses of sv-benchmarks (`c/validation-crafted`) expect. A target without a column is the first statement or
///   full expression in the line, which the call of the error function must start or be innermost in
///   (enclosingStart()).
/// - A block has no evaluation point of its own: an assumption at a `{` is evaluated before the first statement in
///   the block, which runs in the same state. An empty block, and one whose first statement has a label (a jump can
///   enter there without passing the `{`), have none.
class LocationMap {
public:
    /// `originalFile` is FileId::None unless `file` is the preprocessor's output; `unit` is the program's AST.
    LocationMap(const SourceManager& sources, FileId file, FileId originalFile, const ast::TranslationUnit& unit);

    [[nodiscard]] Place place(const witness::Location& location, witness::WaypointType type) const;

    /// For a preprocessed program: the first line of the original file with a line directive (`#line 7`, `# 7`), which
    /// would make presumed lines differ from the lines a witness counts; nullopt if there is none.
    [[nodiscard]] std::optional<std::uint32_t> lineDirective() const { return lineDirective_; }

    /// "line:column" of a program location as the witness counts them, for messages. In a preprocessed program, the
    /// column is one of the original line; a location after a change by the preprocessor is "line (column c after
    /// preprocessing)", one in another file (a header) "file:line:column".
    [[nodiscard]] std::string describe(SourceLocation location) const;

    /// The line and column a witness gives for a location in the program file, for writing witnesses; nullopt for one
    /// that a witness cannot give: after a change by the preprocessor, or in another file (a header).
    [[nodiscard]] std::optional<LineColumn> witnessPosition(SourceLocation location) const;

    /// The start of the innermost statement or full expression (C17 6.8p4) that contains the call, where the format
    /// puts the target of a violation in the call (sv-witnesses, Witness-Format.md, "Target"); the call's own start for
    /// a call outside the program's functions (in a constraint).
    [[nodiscard]] SourceLocation enclosingStart(const ast::CallExpr* call) const;

private:
    struct Point {
        std::uint32_t offset = 0;    // where the construct begins
        std::uint32_t evaluated = 0; // where it is evaluated: different for blocks and `do`
        const ast::FunctionDecl* function = nullptr;
    };

    void collectPoints(const ast::FunctionDecl& function);
    /// The physical lines of the file that a witness line refers to.
    [[nodiscard]] std::vector<std::uint32_t> physicalLinesOf(std::uint32_t line) const;
    /// A physical line as a byte range.
    [[nodiscard]] Place::Line rangeOf(std::uint32_t physicalLine) const;
    [[nodiscard]] const std::vector<Point>& pointsOf(witness::WaypointType type) const;

    const SourceManager& sources_;
    FileId file_;
    FileId originalFile_;
    // Preprocessed programs only: the physical lines of each presumed line of the original file, and per physical line
    // (index line - 1) the number of leading bytes in which it agrees with its original line (0 for other files).
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> physicalLines_;
    std::vector<std::uint32_t> agreeing_;
    std::optional<std::uint32_t> lineDirective_;
    // The evaluation points of the program, sorted by offset.
    std::vector<Point> statements_;
    std::vector<Point> branchings_;
    std::vector<Point> calls_;
    // The offsets where statements and full expressions begin, sorted: for targets without a column.
    std::vector<std::uint32_t> starts_;
    // Per call of the program, where its innermost statement or full expression begins: for targets.
    std::unordered_map<const ast::CallExpr*, SourceLocation> enclosingStarts_;
};

} // namespace nitwit::validate
