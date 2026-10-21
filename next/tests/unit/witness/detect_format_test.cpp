#include <gtest/gtest.h>

#include "witness/parse.hpp"

namespace nitwit::witness {
namespace {

TEST(DetectFormatTest, XmlIsGraphml) {
    EXPECT_EQ(detectFormat("<?xml version=\"1.0\"?>\n<graphml/>"), WitnessFormat::Graphml);
    EXPECT_EQ(detectFormat("<graphml/>"), WitnessFormat::Graphml);
}

TEST(DetectFormatTest, WhitespaceAndByteOrderMarkAreSkipped) {
    EXPECT_EQ(detectFormat("\xEF\xBB\xBF \r\n\t<graphml/>"), WitnessFormat::Graphml);
    EXPECT_EQ(detectFormat("\xEF\xBB\xBF- entry_type: violation_sequence"), WitnessFormat::Yaml);
}

TEST(DetectFormatTest, AnythingElseIsYaml) {
    EXPECT_EQ(detectFormat("- entry_type: violation_sequence"), WitnessFormat::Yaml);
    EXPECT_EQ(detectFormat("# <graphml> in a comment\n- entry_type: x"), WitnessFormat::Yaml);
    EXPECT_EQ(detectFormat("[]"), WitnessFormat::Yaml);
}

TEST(DetectFormatTest, EmptyInputIsYaml) {
    EXPECT_EQ(detectFormat(""), WitnessFormat::Yaml);
    EXPECT_EQ(detectFormat(" \n"), WitnessFormat::Yaml);
    EXPECT_EQ(detectFormat("\xEF\xBB\xBF"), WitnessFormat::Yaml);
}

} // namespace
} // namespace nitwit::witness
