// Witness locations as places in the program (location.hpp; validate/README.md, "Evaluation points").

#include "validate/location.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace nitwit::validate {
namespace {

witness::Location at(std::uint32_t line, std::optional<std::uint32_t> column = std::nullopt) {
    return {.fileName = "test.c", .line = line, .column = column, .function = ""};
}

class LocationTest : public ::testing::Test {
protected:
    // Physical lines: 1 "int a;", 2 "\tint b; int c;", 3 "int d;" (a tab counts as one column).
    void SetUp() override { file_ = sources_.addFile("test.c", "int a;\n\tint b; int c;\r\nint d;"); }

    [[nodiscard]] SourceLocation offset(std::uint32_t offset) const { return {file_, offset}; }

    SourceManager sources_;
    FileId file_ = FileId::None;
};

TEST_F(LocationTest, WithoutAColumnEveryLocationInTheLineMatches) {
    const Place place = LocationMap(sources_, file_, false).place(at(2));
    EXPECT_FALSE(place.contains(offset(6)));  // the newline of line 1
    EXPECT_TRUE(place.contains(offset(7)));   // the tab
    EXPECT_TRUE(place.contains(offset(15)));  // "int c"
    EXPECT_TRUE(place.contains(offset(22)));  // the line feed after "\r"
    EXPECT_FALSE(place.contains(offset(23))); // line 3
}

TEST_F(LocationTest, AColumnCountsBytesFromOne) {
    const LocationMap map(sources_, file_, false);
    EXPECT_TRUE(map.place(at(2, 2)).contains(offset(8))); // "int b" after the tab
    EXPECT_FALSE(map.place(at(2, 2)).contains(offset(7)));
    EXPECT_TRUE(map.place(at(2, 9)).contains(offset(15))); // "int c"
    EXPECT_TRUE(map.place(at(1, 1)).contains(offset(0)));
}

TEST_F(LocationTest, AColumnBeyondItsLineMatchesNothing) {
    const Place place = LocationMap(sources_, file_, false).place(at(1, 9));
    for (std::uint32_t i = 0; i <= 25; ++i) {
        EXPECT_FALSE(place.contains(offset(i))) << i;
    }
}

TEST_F(LocationTest, LinesBeyondTheFileMatchNothing) {
    const Place place = LocationMap(sources_, file_, false).place(at(4));
    for (std::uint32_t i = 0; i <= 25; ++i) {
        EXPECT_FALSE(place.contains(offset(i))) << i;
    }
}

TEST_F(LocationTest, LocationsInOtherFilesNeverMatch) {
    const FileId other = sources_.addFile("other.c", "int a;\n");
    EXPECT_FALSE(LocationMap(sources_, file_, false).place(at(1)).contains({other, 0}));
}

TEST_F(LocationTest, DescribeGivesLineAndColumn) {
    EXPECT_EQ(LocationMap(sources_, file_, false).describe(offset(15)), "2:9");
}

class PreprocessedLocationTest : public ::testing::Test {
protected:
    // The output of cpp for prog.c with an included header; the markers are those the lexer registers.
    void SetUp() override {
        file_ = sources_.addFile("prog.c", "# 1 \"prog.c\"\n"   // 1
                                           "# 1 \"h.h\" 1\n"    // 2
                                           "int h;\n"           // 3: h.h:1
                                           "# 2 \"prog.c\" 2\n" // 4
                                           "int a;\n"           // 5: prog.c:2
                                           "int b; int c;\n"    // 6: prog.c:3
                                           "# 7 \"prog.c\"\n"   // 7
                                           "int d;\n");         // 8: prog.c:7
        sources_.addLineMarker(file_, 2, 1, "prog.c");
        sources_.addLineMarker(file_, 3, 1, "h.h");
        sources_.addLineMarker(file_, 5, 2, "prog.c");
        sources_.addLineMarker(file_, 8, 7, "prog.c");
    }

    [[nodiscard]] SourceLocation line(std::uint32_t physical, std::uint32_t column = 1) const {
        return *sources_.location(file_, {physical, column});
    }

    SourceManager sources_;
    FileId file_ = FileId::None;
};

TEST_F(PreprocessedLocationTest, WitnessLinesArePresumedLinesOfTheOriginalFile) {
    const LocationMap map(sources_, file_, true);
    EXPECT_TRUE(map.place(at(2)).contains(line(5)));
    EXPECT_TRUE(map.place(at(3, 8)).contains(line(6, 8)));
    EXPECT_FALSE(map.place(at(3, 8)).contains(line(6, 1)));
    EXPECT_TRUE(map.place(at(7)).contains(line(8)));
    EXPECT_FALSE(map.place(at(5)).contains(line(5)));
}

TEST_F(PreprocessedLocationTest, LinesOfOtherFilesNeverMatch) {
    // Line 3 is line 1 of h.h, and line 1 of prog.c has no code.
    EXPECT_FALSE(LocationMap(sources_, file_, true).place(at(1)).contains(line(3)));
}

TEST_F(PreprocessedLocationTest, DescribeGivesPresumedLines) {
    EXPECT_EQ(LocationMap(sources_, file_, true).describe(line(6, 8)), "3:8");
}

} // namespace
} // namespace nitwit::validate
