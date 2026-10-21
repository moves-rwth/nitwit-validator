// The validation mode of the driver (driver/README.md): files, property, front end, witness format, result line and
// exit codes (docs/exit-codes.md).

#include "driver/validation.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>

#include <gtest/gtest.h>

namespace nitwit::driver {
namespace {

TEST(ErrorFunctionTest, UnreachCallNamesTheErrorFunction) {
    EXPECT_EQ(errorFunctionOf("CHECK( init(main()), LTL(G ! call(reach_error())) )\n"), "reach_error");
    EXPECT_EQ(errorFunctionOf("CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )"), "__VERIFIER_error");
    EXPECT_EQ(errorFunctionOf("CHECK(init(main()),LTL(G!call(fail())))"), "fail");
}

TEST(ErrorFunctionTest, OtherPropertiesAreNotSupported) {
    EXPECT_EQ(errorFunctionOf("CHECK( init(main()), LTL(G valid-free) )"), std::nullopt);
    EXPECT_EQ(errorFunctionOf("CHECK( init(main()), LTL(G ! overflow) )"), std::nullopt);
    EXPECT_EQ(errorFunctionOf("CHECK( init(main()), LTL(F end) )"), std::nullopt);
    EXPECT_EQ(errorFunctionOf(""), std::nullopt);
}

constexpr std::string_view program = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a == 5) {
        reach_error();
    }
    return 0;
}
)";

constexpr std::string_view witness = R"(- entry_type: violation_sequence
  content:
  - segment:
    - waypoint:
        type: assumption
        action: follow
        location: {file_name: program.c, line: 5, column: 5}
        constraint: {value: a == 5, format: c_expression}
  - segment:
    - waypoint:
        type: target
        action: follow
        location: {file_name: program.c, line: 6, column: 9}
)";

class ValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::path(::testing::TempDir()) / ("nitwit-validation-" + std::to_string(::getpid()));
        std::filesystem::create_directories(directory_);
        command_.program = write("program.c", program);
        command_.witness = write("witness.yml", witness);
    }

    void TearDown() override { std::filesystem::remove_all(directory_); }

    std::string write(std::string_view name, std::string_view text) const {
        const std::filesystem::path path = directory_ / name;
        std::ofstream(path, std::ios::binary) << text;
        return path.string();
    }

    /// Runs the validation; its output is in output_.
    int run() {
        output_.str("");
        log_.str("");
        return validateWitness(command_, output_, log_);
    }

    /// The last line of the output.
    [[nodiscard]] std::string resultLine() const {
        std::string text = output_.str();
        if (text.ends_with('\n')) {
            text.pop_back();
        }
        return text.substr(text.rfind('\n') + 1);
    }

    std::filesystem::path directory_;
    ValidationCommand command_;
    std::ostringstream output_;
    std::ostringstream log_;
};

TEST_F(ValidationTest, AConfirmedWitnessIsValidated) {
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(resultLine(), "VALIDATED: the witness was followed to its target: 'reach_error' was called at 6:9 in "
                            "segment 2 of 2");
    EXPECT_EQ(log_.str(), "");
}

TEST_F(ValidationTest, TheLogGoesToItsStreamWithVerbose) {
    command_.verbose = true;
    EXPECT_EQ(run(), 0);
    EXPECT_TRUE(log_.str().contains("run 1: fixing input 0")) << log_.str();
}

TEST_F(ValidationTest, AWitnessThatIsNotConfirmedFails) {
    command_.errorFunction = "__VERIFIER_error";
    EXPECT_EQ(run(), 250);
    EXPECT_TRUE(resultLine().starts_with("FAILED: the target segment was reached")) << resultLine();
}

TEST_F(ValidationTest, ThePropertyFileNamesTheErrorFunction) {
    command_.propertyFile = write("unreach-call.prp", "CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )\n");
    EXPECT_EQ(run(), 250);
}

TEST_F(ValidationTest, OtherPropertiesAreUnknown) {
    command_.propertyFile = write("valid-memsafety.prp", "CHECK( init(main()), LTL(G valid-free) )\n");
    EXPECT_EQ(run(), 4);
    EXPECT_EQ(resultLine(), "UNKNOWN: NITWIT only supports the property unreach-call");
}

TEST_F(ValidationTest, UnreadableFilesAreErrorsOfTheCommandLine) {
    command_.witness = (directory_ / "missing.yml").string();
    EXPECT_EQ(run(), 3);
    command_.witness = write("witness.yml", witness);
    command_.program = (directory_ / "missing.c").string();
    EXPECT_EQ(run(), 3);
    command_.program = write("program.c", program);
    command_.propertyFile = (directory_ / "missing.prp").string();
    EXPECT_EQ(run(), 3);
}

TEST_F(ValidationTest, ProgramsTheFrontEndRejectsAreUnknown) {
    command_.program = write("program.c", "int main(void) { return 0 }\n");
    EXPECT_EQ(run(), 4);
    EXPECT_EQ(resultLine(), "UNKNOWN: NITWIT cannot analyze the program");
    EXPECT_TRUE(output_.str().contains("program.c:1:27: error:")) << output_.str();
}

TEST_F(ValidationTest, MalformedWitnessesAreErrors) {
    command_.witness = write("witness.yml", "- entry_type: violation_sequence\n  content: 5\n");
    EXPECT_EQ(run(), 2);
    EXPECT_TRUE(resultLine().starts_with("UNKNOWN: the witness cannot be parsed:")) << resultLine();
}

TEST_F(ValidationTest, GraphmlWitnessesAreNotSupportedYet) {
    command_.witness = write("witness.graphml", "<?xml version=\"1.0\"?><graphml></graphml>\n");
    EXPECT_EQ(run(), 4);
    EXPECT_EQ(resultLine(), "UNKNOWN: witnesses in format 1.0 (GraphML) are not supported yet");
}

TEST_F(ValidationTest, ConstraintErrorsArePrintedAsDiagnostics) {
    std::string text(witness);
    text.replace(text.find("a == 5"), 6, "q == 5");
    command_.witness = write("witness.yml", text);
    EXPECT_EQ(run(), 4);
    EXPECT_TRUE(output_.str().contains("<constraint>:1:1: error: use of undeclared identifier 'q'\n")) << output_.str();
}

TEST_F(ValidationTest, WitnessLinesOfPreprocessedProgramsArePresumedLines) {
    // The directive makes the front end run cpp; the witness refers to the lines of program.c.
    command_.program = write("program.c", std::string("#define FIVE 5\n") + std::string(program));
    std::string text(witness);
    text.replace(text.find("line: 5"), 7, "line: 6");
    text.replace(text.find("line: 6, column: 9"), 7, "line: 7");
    text.replace(text.find("a == 5"), 6, "a == FIVE - 0");
    command_.witness = write("witness.yml", text);
    EXPECT_EQ(run(), 4) << output_.str(); // FIVE is a macro, unknown to constraints
    text.replace(text.find("a == FIVE - 0"), 13, "a == 5");
    command_.witness = write("witness.yml", text);
    EXPECT_EQ(run(), 0) << output_.str();
}

} // namespace
} // namespace nitwit::driver
