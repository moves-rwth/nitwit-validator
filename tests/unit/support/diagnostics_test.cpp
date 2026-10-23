#include "support/diagnostics.hpp"

#include <gtest/gtest.h>

namespace nitwit {
namespace {

TEST(DiagnosticsTest, CountsOnlyErrors) {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    diagnostics.warning({}, "careful");
    EXPECT_FALSE(diagnostics.hasErrors());
    diagnostics.error({}, "broken");
    EXPECT_TRUE(diagnostics.hasErrors());
    EXPECT_EQ(diagnostics.diagnostics().size(), 2u);
}

TEST(DiagnosticsTest, DiagnosticsAfterACountCanBeDiscarded) {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    diagnostics.warning({}, "careful");
    diagnostics.error({}, "broken");
    diagnostics.discardAfter(1);
    EXPECT_FALSE(diagnostics.hasErrors());
    ASSERT_EQ(diagnostics.diagnostics().size(), 1u);
    EXPECT_EQ(diagnostics.diagnostics()[0].message, "careful");
    diagnostics.discardAfter(5);
    EXPECT_EQ(diagnostics.diagnostics().size(), 1u);
}

TEST(DiagnosticsTest, FormatIncludesPositionWhenKnown) {
    SourceManager sources;
    const FileId file = sources.addFile("a.c", "int x;\n");
    DiagnosticEngine diagnostics(sources);
    diagnostics.error({file, 4}, "unexpected 'x'");
    diagnostics.note({}, "no position");
    EXPECT_EQ(diagnostics.format(diagnostics.diagnostics()[0]), "a.c:1:5: error: unexpected 'x'");
    EXPECT_EQ(diagnostics.format(diagnostics.diagnostics()[1]), "note: no position");
}

} // namespace
} // namespace nitwit
