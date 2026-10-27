// The validation mode of the driver (driver/README.md): files, property, front end, witness format, result line and
// exit codes (docs/exit-codes.md). The witness gives its input as a hint, which needs no search.

#include "driver/validation.hpp"

#include <filesystem>
#include <format>
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
        type: function_return
        action: follow
        location: {file_name: program.c, line: 4, column: 35}
        constraint: {value: "\\result == 5", format: acsl_expression}
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
    EXPECT_TRUE(log_.str().contains("run 1: segment 1 of 2 passed at 4:35 (function_return)")) << log_.str();
}

TEST_F(ValidationTest, AConfirmationWritesAConcreteWitnessThatStrictInputsConfirm) {
    command_.witnessOutput = (directory_ / "concrete.yml").string();
    EXPECT_EQ(run(), 0);
    EXPECT_TRUE(resultLine().starts_with("VALIDATED: ")) << resultLine();
    EXPECT_EQ(log_.str(),
              std::format("nitwit: wrote a concrete witness with 1 input to '{}'\n", command_.witnessOutput));
    ValidationCommand strict = command_;
    strict.witness = command_.witnessOutput;
    strict.witnessOutput.clear();
    strict.strictInputs = true;
    command_ = strict;
    EXPECT_EQ(run(), 0) << output_.str();
}

TEST_F(ValidationTest, TheCheckOfTheConcreteWitnessCanBeLeftOut) {
    command_.witnessOutput = (directory_ / "concrete.yml").string();
    command_.checksWitness = false;
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(log_.str(), std::format("nitwit: wrote an unchecked concrete witness with 1 input to '{}'\n",
                                      command_.witnessOutput));
    EXPECT_TRUE(std::filesystem::exists(command_.witnessOutput));
}

TEST_F(ValidationTest, WithoutAConfirmationNoWitnessIsWritten) {
    command_.errorFunction = "__VERIFIER_error";
    command_.witnessOutput = (directory_ / "concrete.yml").string();
    EXPECT_EQ(run(), 250);
    EXPECT_EQ(log_.str(), "nitwit: no witness written: the witness was not confirmed\n");
    EXPECT_FALSE(std::filesystem::exists(command_.witnessOutput));
}

TEST_F(ValidationTest, AWitnessThatCannotBeWrittenKeepsTheVerdict) {
    command_.witnessOutput = (directory_ / "missing" / "concrete.yml").string();
    EXPECT_EQ(run(), 0);
    EXPECT_EQ(log_.str(), std::format("nitwit: no witness written: cannot write '{}'\n", command_.witnessOutput));
}

TEST_F(ValidationTest, StrictInputsNeedEveryValue) {
    command_.witness = write("target-only.yml", R"(- entry_type: violation_sequence
  content:
  - segment:
    - waypoint: {type: target, action: follow, location: {file_name: program.c, line: 6, column: 9}}
)");
    command_.strictInputs = true;
    EXPECT_EQ(run(), 4);
    EXPECT_EQ(resultLine(), "UNKNOWN: the witness does not give the value of input 0 (the call of "
                            "__VERIFIER_nondet_int at 4:35, occurrence 0)");
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
    EXPECT_TRUE(resultLine().starts_with("ERROR: cannot read the witness")) << resultLine();
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
    EXPECT_TRUE(resultLine().starts_with("ERROR: the witness cannot be parsed:")) << resultLine();
}

TEST_F(ValidationTest, GraphmlWitnessesAreNotSupportedYet) {
    command_.witness = write("witness.graphml", "<?xml version=\"1.0\"?><graphml></graphml>\n");
    EXPECT_EQ(run(), 4);
    EXPECT_EQ(resultLine(), "UNKNOWN: witnesses in format 1.0 (GraphML) are not supported yet");
}

TEST_F(ValidationTest, ConstraintErrorsArePrintedAsDiagnostics) {
    std::string text(witness);
    text.replace(text.find("\\\\result == 5"), 13, "q == 5");
    command_.witness = write("witness.yml", text);
    EXPECT_EQ(run(), 4);
    EXPECT_TRUE(output_.str().contains("<constraint>:1:1: error: use of undeclared identifier 'q'\n")) << output_.str();
}

TEST_F(ValidationTest, WitnessLinesOfPreprocessedProgramsArePresumedLines) {
    // The directive makes the front end run cpp; the witness refers to the lines of program.c.
    command_.program = write("program.c", std::string("#define FIVE 5\n") + std::string(program));
    std::string text(witness);
    text.replace(text.find("line: 4"), 7, "line: 5");
    text.replace(text.find("line: 6, column: 9"), 7, "line: 7");
    text.replace(text.find("== 5"), 4, "== FIVE - 0");
    command_.witness = write("witness.yml", text);
    EXPECT_EQ(run(), 4) << output_.str(); // FIVE is a macro, unknown to constraints
    text.replace(text.find("== FIVE - 0"), 11, "== 5");
    command_.witness = write("witness.yml", text);
    EXPECT_EQ(run(), 0) << output_.str();
}

TEST_F(ValidationTest, LineDirectivesInProgramsThatNeedThePreprocessorDoNotFit) {
    // cpp would give line 4 of the dead function and line 8 the same presumed line 4.
    command_.program = write("program.c", "#define UNUSED 1\n"
                                          "void reach_error(void) {}\n"
                                          "void dead(void) {\n"
                                          "  reach_error();\n"
                                          "}\n"
                                          "int main(void) {\n"
                                          "#line 4\n"
                                          "  reach_error();\n"
                                          "  return 0;\n"
                                          "}\n");
    command_.witness = write("witness.yml", R"(- entry_type: violation_sequence
  content:
  - segment:
    - waypoint:
        type: target
        action: follow
        location: {file_name: program.c, line: 4, column: 3}
)");
    EXPECT_EQ(run(), 4) << output_.str();
    EXPECT_EQ(resultLine(), "UNKNOWN: the witness does not fit the program: a #line directive (line 7) in a program "
                            "that needs the preprocessor: its presumed lines are not the lines of the witness");
}

TEST_F(ValidationTest, ColumnsAfterAChangeByThePreprocessorDoNotFitTheProgram) {
    // cpp collapses the blanks before the second if of line 6, so that column 25 of its output is the third if, whose
    // condition holds; in the original line, it is the second if, whose condition does not.
    command_.program = write("program.c", "#define ZERO 0\n"
                                          "extern int __VERIFIER_nondet_int(void);\n"
                                          "extern void reach_error(void);\n"
                                          "int main(void) {\n"
                                          "    int a = __VERIFIER_nondet_int(); int b = ZERO;\n"
                                          "    if (a) {}           if (b) {} if (!b) reach_error();\n"
                                          "    return 0;\n"
                                          "}\n");
    command_.witness = write("witness.yml", R"(- entry_type: violation_sequence
  content:
  - segment:
    - waypoint:
        type: branching
        action: follow
        location: {file_name: program.c, line: 6, column: 25}
        constraint: {value: "true", format: c_expression}
  - segment:
    - waypoint:
        type: target
        action: follow
        location: {file_name: program.c, line: 6}
)");
    EXPECT_EQ(run(), 4) << output_.str();
    EXPECT_EQ(resultLine(), "UNKNOWN: the witness does not fit the program: the follow waypoint of segment 1 "
                            "(branching at 6:25) is at no evaluation point of its type: its column lies after text "
                            "that the preprocessor changed");
}

} // namespace
} // namespace nitwit::driver
