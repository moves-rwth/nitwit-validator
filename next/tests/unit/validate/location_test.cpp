// Witness locations as places in the program (location.hpp; validate/README.md, "Evaluation points").

#include "validate/location.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "validate_test_support.hpp"

namespace nitwit::validate {
namespace {

using witness::WaypointType;

witness::Location at(std::uint32_t line, std::optional<std::uint32_t> column = std::nullopt) {
    return {.fileName = "test.c", .line = line, .column = column, .function = ""};
}

class LocationTest : public test::ValidateTest {
protected:
    [[nodiscard]] Place place(WaypointType type, std::uint32_t line,
                              std::optional<std::uint32_t> column = std::nullopt) const {
        return LocationMap(sources_, file_, original_, unit()).place(at(line, column), type);
    }

    [[nodiscard]] SourceLocation position(std::uint32_t line, std::uint32_t column) const {
        return *sources_.location(file_, {line, column});
    }

    /// Whether the place contains any location of the file.
    [[nodiscard]] bool isEmpty(const Place& place) const {
        for (std::uint32_t offset = 0; offset <= sources_.contents(file_).size(); ++offset) {
            if (place.contains({file_, offset})) {
                return false;
            }
        }
        return true;
    }

    FileId original_ = FileId::None;
};

// Line 5 starts with a tab, which counts as one column.
constexpr std::string_view source = "extern int __VERIFIER_nondet_int(void);\n"
                                    "void reach_error(void);\n"
                                    "int main(void) {\n"
                                    "    int a = __VERIFIER_nondet_int(); int b = __VERIFIER_nondet_int();\n"
                                    "\tif (a) { b = a ? 1 : 2; } while (b) b--;\n"
                                    "    {\n"
                                    "        {\n"
                                    "            a = 1;\n"
                                    "        }\n"
                                    "        {}\n"
                                    "    }\n"
                                    "    reach_error();\n"
                                    "    return a;\n"
                                    "}\n";

class ProgramLocationTest : public LocationTest {
protected:
    void SetUp() override { program(std::string(source)); }
};

TEST_F(ProgramLocationTest, AColumnCountsBytesFromOne) {
    EXPECT_TRUE(place(WaypointType::Branching, 5, 17).contains(position(5, 17))); // the ?
    EXPECT_FALSE(place(WaypointType::Branching, 5, 17).contains(position(5, 2)));
    EXPECT_TRUE(place(WaypointType::Branching, 5, 28).contains(position(5, 28))); // while
}

TEST_F(ProgramLocationTest, WithoutAColumnTheFirstEvaluationPointOfTheTypeInTheLineCounts) {
    const Place branching = place(WaypointType::Branching, 5);
    EXPECT_TRUE(branching.contains(position(5, 2)));
    EXPECT_FALSE(branching.contains(position(5, 17)));
    EXPECT_FALSE(branching.contains(position(5, 28)));
    EXPECT_TRUE(place(WaypointType::Assumption, 5).contains(position(5, 2)));
    EXPECT_FALSE(place(WaypointType::Assumption, 5).contains(position(5, 11)));
    EXPECT_TRUE(place(WaypointType::FunctionReturn, 4).contains(position(4, 35)));
    EXPECT_FALSE(place(WaypointType::FunctionReturn, 4).contains(position(4, 68)));
    EXPECT_TRUE(place(WaypointType::FunctionEnter, 4).contains(position(4, 35)));
}

TEST_F(ProgramLocationTest, ALineWithoutEvaluationPointsMatchesNothing) {
    EXPECT_TRUE(isEmpty(place(WaypointType::Branching, 4)));
    EXPECT_TRUE(isEmpty(place(WaypointType::FunctionReturn, 13)));
}

TEST_F(ProgramLocationTest, AColumnBeyondItsLineMatchesNothing) {
    EXPECT_TRUE(isEmpty(place(WaypointType::Assumption, 2, 30)));
}

TEST_F(ProgramLocationTest, LinesBeyondTheFileMatchNothing) {
    EXPECT_TRUE(isEmpty(place(WaypointType::Assumption, 99)));
    EXPECT_TRUE(isEmpty(place(WaypointType::Target, 99)));
}

TEST_F(ProgramLocationTest, ATargetWithoutAColumnMatchesTheWholeLine) {
    EXPECT_TRUE(place(WaypointType::Target, 12).contains(position(12, 5)));
    EXPECT_TRUE(place(WaypointType::Target, 12).contains(position(12, 10)));
    EXPECT_FALSE(place(WaypointType::Target, 12, 5).contains(position(12, 6)));
    EXPECT_TRUE(place(WaypointType::Target, 12, 5).contains(position(12, 5)));
}

TEST_F(ProgramLocationTest, AnAssumptionAtABlockIsEvaluatedBeforeItsFirstStatement) {
    const SourceLocation assignment = position(8, 13);
    EXPECT_TRUE(place(WaypointType::Assumption, 6, 5).contains(assignment));
    EXPECT_TRUE(place(WaypointType::Assumption, 6).contains(assignment));
    EXPECT_TRUE(place(WaypointType::Assumption, 7, 9).contains(assignment));
    EXPECT_TRUE(place(WaypointType::Assumption, 5, 9).contains(position(5, 11)));
    EXPECT_FALSE(place(WaypointType::Branching, 6, 5).contains(assignment));
    // An empty block has no first statement.
    EXPECT_FALSE(place(WaypointType::Assumption, 10, 9).contains(position(12, 5)));
}

TEST_F(ProgramLocationTest, LocationsInOtherFilesNeverMatch) {
    const FileId other = sources_.addFile("other.c", std::string(source));
    EXPECT_FALSE(place(WaypointType::Branching, 5, 2).contains({other, position(5, 2).offset}));
}

TEST_F(ProgramLocationTest, DescribeGivesLineAndColumn) {
    EXPECT_EQ(LocationMap(sources_, file_, FileId::None, unit()).describe(position(5, 17)), "5:17");
}

// prog.c and the output of cpp for it, with an included header. The second if in line 6 stands for a macro expansion,
// which makes the line longer than the original.
class PreprocessedLocationTest : public LocationTest {
protected:
    void SetUp() override {
        original_ = sources_.addFile("prog.c", "#include \"h.h\"\n"
                                               "int main(void) {\n"
                                               "    int a = 0; if (a) a = 1;\n"
                                               "    return a;\n"
                                               "}\n");
        program("# 1 \"prog.c\"\n"
                "# 1 \"h.h\" 1\n"
                "int h(void) { if (1) return 1; return 0; }\n"
                "# 2 \"prog.c\" 2\n"
                "int main(void) {\n"
                "    int a = 0; if (a) a = 1; if (a) a = 2;\n"
                "    return a;\n"
                "}\n",
                DataModel::ILP32, "prog.c");
    }
};

TEST_F(PreprocessedLocationTest, WitnessLinesArePresumedLinesOfTheOriginalFile) {
    EXPECT_TRUE(place(WaypointType::Branching, 3, 16).contains(position(6, 16)));
    EXPECT_TRUE(place(WaypointType::Branching, 3).contains(position(6, 16)));
    EXPECT_TRUE(place(WaypointType::Assumption, 4, 5).contains(position(7, 5)));
}

TEST_F(PreprocessedLocationTest, LinesOfOtherFilesNeverMatch) {
    // Physical line 3 is line 1 of h.h; line 1 of prog.c has no code.
    EXPECT_FALSE(place(WaypointType::Branching, 1).contains(position(3, 15)));
    EXPECT_FALSE(place(WaypointType::Branching, 1, 15).contains(position(3, 15)));
}

TEST_F(PreprocessedLocationTest, ColumnsBeyondTheOriginalLineMatchNothing) {
    EXPECT_TRUE(isEmpty(place(WaypointType::Branching, 3, 30)));
}

TEST_F(PreprocessedLocationTest, DescribeGivesPresumedLines) {
    EXPECT_EQ(LocationMap(sources_, file_, original_, unit()).describe(position(6, 16)), "3:16");
}

} // namespace
} // namespace nitwit::validate
