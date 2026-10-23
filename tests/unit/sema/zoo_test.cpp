// The type-and-value zoo in data/: static assertions about the types and values of expressions, which sema checks
// here and gcc-14 checks in the CTests of CMakeLists.txt, so that both agree.

#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

class ZooTest : public SemaTest {
protected:
    static std::string read(std::string_view name) {
        const std::filesystem::path path = std::filesystem::path(NITWIT_TEST_DATA_DIR) / name;
        std::ifstream input(path);
        EXPECT_TRUE(input.good()) << "cannot read " << path;
        std::stringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }

    // Analyzes a zoo file, which checks all its assertions, and returns the first error or warning. The parser warns
    // about the implicit declaration of the atomic builtins, which GCC knows without a declaration.
    std::string check(std::string_view name, DataModel model) {
        if (analyze(read(name), model) == nullptr) {
            return firstError();
        }
        for (const std::string& warning : warnings()) {
            if (!warning.starts_with("implicit declaration of function '__atomic_")
                && !warning.starts_with("implicit declaration of function '__sync_")) {
                return "warning: " + warning;
            }
        }
        return "";
    }
};

TEST_F(ZooTest, CommonToBothDataModels) {
    EXPECT_EQ(check("zoo_common.c", DataModel::ILP32), "");
    EXPECT_EQ(check("zoo_common.c", DataModel::LP64), "");
}

TEST_F(ZooTest, Ilp32) {
    EXPECT_EQ(check("zoo_ilp32.c", DataModel::ILP32), "");
}

TEST_F(ZooTest, Lp64) {
    EXPECT_EQ(check("zoo_lp64.c", DataModel::LP64), "");
}

} // namespace
} // namespace nitwit::sema
