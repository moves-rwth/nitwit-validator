#include "support/source_manager.hpp"

#include <gtest/gtest.h>

namespace nitwit {
namespace {

TEST(SourceManagerTest, LineAndColumnAreOneBased) {
    SourceManager sources;
    const FileId file = sources.addFile("a.c", "int x;\n\tint y;\n");
    EXPECT_EQ(sources.lineColumn({file, 0}), (LineColumn{1, 1}));
    EXPECT_EQ(sources.lineColumn({file, 4}), (LineColumn{1, 5}));
    EXPECT_EQ(sources.lineColumn({file, 7}), (LineColumn{2, 1}));
    // A tab counts as one column.
    EXPECT_EQ(sources.lineColumn({file, 8}), (LineColumn{2, 2}));
}

TEST(SourceManagerTest, EndOfFileHasAPosition) {
    SourceManager sources;
    const FileId file = sources.addFile("a.c", "x\n");
    EXPECT_EQ(sources.lineColumn({file, 2}), (LineColumn{2, 1}));
    EXPECT_EQ(sources.lineCount(file), 2u);
}

TEST(SourceManagerTest, LocationIsInverseOfLineColumn) {
    SourceManager sources;
    const FileId file = sources.addFile("a.c", "ab\ncde\n");
    const auto location = sources.location(file, {2, 3});
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, 5u);
    EXPECT_EQ(sources.lineColumn(*location), (LineColumn{2, 3}));
}

TEST(SourceManagerTest, LocationRejectsPositionsOutsideTheFile) {
    SourceManager sources;
    const FileId file = sources.addFile("a.c", "ab\ncde\n");
    EXPECT_FALSE(sources.location(file, {0, 1}).has_value());
    EXPECT_FALSE(sources.location(file, {1, 0}).has_value());
    EXPECT_FALSE(sources.location(file, {1, 5}).has_value());
    EXPECT_FALSE(sources.location(file, {4, 1}).has_value());
    // One past the end of a line is allowed.
    EXPECT_TRUE(sources.location(file, {1, 3}).has_value());
}

TEST(SourceManagerTest, LineTextStripsLineTerminators) {
    SourceManager sources;
    const FileId file = sources.addFile("a.c", "first\r\nsecond\nlast");
    EXPECT_EQ(sources.lineText(file, 1), "first");
    EXPECT_EQ(sources.lineText(file, 2), "second");
    EXPECT_EQ(sources.lineText(file, 3), "last");
}

TEST(SourceManagerTest, PresumedLocationFollowsLineMarkers) {
    SourceManager sources;
    const FileId file = sources.addFile("a.i", "# 1 \"a.c\"\nint x;\n# 10 \"b.h\"\nint y;\nint z;\n");
    sources.addLineMarker(file, 2, 1, "a.c");
    sources.addLineMarker(file, 4, 10, "b.h");

    const PresumedLocation x = sources.presumed({file, 10});
    EXPECT_EQ(x.fileName, "a.c");
    EXPECT_EQ(x.line, 1u);

    const PresumedLocation z = sources.presumed(*sources.location(file, {5, 5}));
    EXPECT_EQ(z.fileName, "b.h");
    EXPECT_EQ(z.line, 11u);
    EXPECT_EQ(z.column, 5u);
}

TEST(SourceManagerTest, PresumedLocationWithoutMarkersIsPhysical) {
    SourceManager sources;
    const FileId file = sources.addFile("a.c", "int x;\n");
    const PresumedLocation location = sources.presumed({file, 4});
    EXPECT_EQ(location.fileName, "a.c");
    EXPECT_EQ(location.line, 1u);
    EXPECT_EQ(location.column, 5u);
}

TEST(SourceManagerTest, DescribeNamesFileLineAndColumn) {
    SourceManager sources;
    const FileId file = sources.addFile("a.c", "int x;\nint y;\n");
    EXPECT_EQ(sources.describe({file, 11}), "a.c:2:5");
}

TEST(SourceManagerTest, LoadFileReportsMissingFiles) {
    SourceManager sources;
    const auto result = sources.loadFile("does/not/exist.c");
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find("cannot open"), std::string::npos);
}

} // namespace
} // namespace nitwit
