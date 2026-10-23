#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "library_test_support.hpp"

namespace nitwit::interp::test {
namespace {

/// One line of data/ctype_c_locale.txt: what glibc's tables and functions give for one argument.
struct CtypeRow {
    int c = 0;
    int classBits = 0;
    int lowerEntry = 0;
    int upperEntry = 0;
    std::array<int, 16> functions{}; // in the order of functionNames
};

constexpr std::array functionNames = {"isalnum", "isalpha", "isblank", "iscntrl", "isdigit", "isgraph",
                                      "islower", "isprint", "ispunct", "isspace", "isupper", "isxdigit",
                                      "isascii", "toascii", "tolower", "toupper"};

std::vector<CtypeRow> readRows() {
    std::ifstream file(NITWIT_TEST_DATA_DIR "/ctype_c_locale.txt");
    std::vector<CtypeRow> rows;
    for (std::string line; std::getline(file, line);) {
        if (line.starts_with('#')) {
            continue;
        }
        std::istringstream fields(line);
        CtypeRow row;
        fields >> row.c >> row.classBits >> row.lowerEntry >> row.upperEntry;
        for (int& result : row.functions) {
            fields >> result;
        }
        rows.push_back(row);
    }
    return rows;
}

class CtypeTest : public LibraryTest {
protected:
    /// The entry for `c` of the table that the pointer returned by `locationFunction` points to.
    Int128 tableEntry(const char* locationFunction, int c, ScalarType entryType) {
        const Address location = callPointer(locationFunction, {});
        const auto entryZero = static_cast<Address>(readInteger(location, ScalarType::Pointer));
        const std::uint64_t entrySize = context.target().layout(entryType).size;
        return readInteger(
            entryZero + static_cast<Address>(static_cast<std::int64_t>(c) * static_cast<std::int64_t>(entrySize)),
            entryType);
    }
};

INSTANTIATE_TEST_SUITE_P(DataModels, CtypeTest, testing::Values(DataModel::ILP32, DataModel::LP64), modelName);

TEST_P(CtypeTest, TablesAreGlibcsCLocaleTables) {
    const std::vector<CtypeRow> rows = readRows();
    ASSERT_EQ(rows.size(), 384u);
    for (const CtypeRow& row : rows) {
        SCOPED_TRACE(row.c);
        EXPECT_EQ(tableEntry("__ctype_b_loc", row.c, ScalarType::UnsignedShort), row.classBits);
        EXPECT_EQ(tableEntry("__ctype_tolower_loc", row.c, ScalarType::Int), row.lowerEntry);
        EXPECT_EQ(tableEntry("__ctype_toupper_loc", row.c, ScalarType::Int), row.upperEntry);
    }
}

TEST_P(CtypeTest, FunctionsReturnWhatGlibcsReturn) {
    for (const CtypeRow& row : readRows()) {
        for (std::size_t f = 0; f < functionNames.size(); ++f) {
            EXPECT_EQ(callInteger(functionNames[f], {intValue(row.c)}), row.functions[f])
                << functionNames[f] << "(" << row.c << ")";
        }
    }
}

TEST_P(CtypeTest, TablesAreReadOnly) {
    const auto entryZero = static_cast<Address>(readInteger(callPointer("__ctype_b_loc", {}), ScalarType::Pointer));
    EXPECT_EQ(memory().find(entryZero)->region, Region::ReadOnly);
    const std::array<std::byte, 2> bytes{};
    EXPECT_FALSE(memory().write(entryZero, bytes).has_value());
    // The pointers to them are writable variables, as in glibc.
    EXPECT_EQ(memory().find(callPointer("__ctype_b_loc", {}))->region, Region::Global);
}

TEST_P(CtypeTest, BuiltinIsdigitReturnsOne) {
    // GCC expands it to (unsigned)(c - '0') <= 9, unlike glibc's isdigit, which returns the class bit.
    EXPECT_EQ(callInteger("__builtin_isdigit", {intValue('7')}), 1);
    EXPECT_EQ(callInteger("isdigit", {intValue('7')}), 2048);
    EXPECT_EQ(callInteger("__builtin_isdigit", {intValue(1000)}), 0);
    EXPECT_EQ(callInteger("__builtin_isalpha", {intValue('a')}), 1024);
}

TEST_P(CtypeTest, LocationsAreStable) {
    const Address location = callPointer("__ctype_b_loc", {});
    EXPECT_EQ(callPointer("__ctype_b_loc", {}), location);
    EXPECT_NE(callPointer("__ctype_tolower_loc", {}), location);
}

TEST_P(CtypeTest, ArgumentsOutsideTheTablesAreUndefinedForClassification) {
    EXPECT_TRUE(isUndefined(call("isalpha", {intValue(256)}), "neither an unsigned char nor EOF"));
    EXPECT_TRUE(isUndefined(call("isdigit", {intValue(-129)}), "neither an unsigned char nor EOF"));
}

TEST_P(CtypeTest, ConversionsReturnArgumentsOutsideTheTablesUnchanged) {
    EXPECT_EQ(callInteger("tolower", {intValue(1000)}), 1000);
    EXPECT_EQ(callInteger("toupper", {intValue(-1000)}), -1000);
    EXPECT_EQ(callInteger("isascii", {intValue(1000)}), 0);
    EXPECT_EQ(callInteger("toascii", {intValue(1000)}), 1000 & 0x7F);
}

TEST_P(CtypeTest, MbCurMaxIsOne) {
    const LibraryResult result = call("__ctype_get_mb_cur_max", {});
    EXPECT_EQ(returned(result), 1);
    EXPECT_EQ(result.value.signedBits(), value::Value::integer(1, format(context.target().sizeType())).signedBits());
}

} // namespace
} // namespace nitwit::interp::test
