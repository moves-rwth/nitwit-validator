// The command line of nitwit (driver/README.md, "Command line"; docs/exit-codes.md: 3 and 240).

#include "driver/command_line.hpp"

#include <initializer_list>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace nitwit::driver {
namespace {

std::expected<CommandLine, int> parse(std::initializer_list<std::string_view> arguments,
                                      DataModel defaultModel = DataModel::ILP32) {
    const std::vector<std::string_view> list(arguments);
    return parseCommandLine(list, defaultModel);
}

/// The exit code of a wrong command line; 0 if it is right.
int exitOf(std::initializer_list<std::string_view> arguments) {
    const std::expected<CommandLine, int> parsed = parse(arguments);
    return parsed ? 0 : parsed.error();
}

TEST(CommandLineTest, AValidation) {
    const auto parsed = parse({"-64", "-w", "w.yml", "-p", "unreach-call.prp", "-v", "prog.c"});
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    ASSERT_TRUE(parsed->validation.has_value());
    EXPECT_FALSE(parsed->replay.has_value());
    EXPECT_EQ(parsed->validation->witness, "w.yml");
    EXPECT_EQ(parsed->validation->propertyFile, "unreach-call.prp");
    EXPECT_EQ(parsed->validation->errorFunction, "");
    EXPECT_EQ(parsed->validation->program, "prog.c");
    EXPECT_EQ(parsed->validation->dataModel, DataModel::LP64);
    EXPECT_TRUE(parsed->validation->verbose);
}

TEST(CommandLineTest, StrictInputsAndAConcreteWitness) {
    const auto parsed = parse({"-w", "w.yml", "--strict-inputs", "--write-witness", "out.yml", "prog.c"});
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    ASSERT_TRUE(parsed->validation.has_value());
    EXPECT_TRUE(parsed->validation->strictInputs);
    EXPECT_EQ(parsed->validation->witnessOutput, "out.yml");
    EXPECT_TRUE(parsed->validation->checksWitness);
    const auto plain = parse({"-w", "w.yml", "prog.c"});
    ASSERT_TRUE(plain.has_value());
    EXPECT_FALSE(plain->validation->strictInputs);
    EXPECT_EQ(plain->validation->witnessOutput, "");
    const auto unchecked = parse({"-w", "w.yml", "--write-witness", "out.yml", "--no-witness-check", "prog.c"});
    ASSERT_TRUE(unchecked.has_value());
    EXPECT_FALSE(unchecked->validation->checksWitness);
}

TEST(CommandLineTest, LongOptionsAndTheDefaultDataModel) {
    const auto parsed = parse({"prog.c", "--witness", "w.yml", "--error-function", "fail"});
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    ASSERT_TRUE(parsed->validation.has_value());
    EXPECT_EQ(parsed->validation->errorFunction, "fail");
    EXPECT_EQ(parsed->validation->dataModel, DataModel::ILP32);
}

TEST(CommandLineTest, TheDefaultDataModelComesFromTheExecutableName) {
    EXPECT_EQ(defaultDataModel("/opt/nitwit/bin/nitwit64"), DataModel::LP64);
    EXPECT_EQ(defaultDataModel("../bin/nitwit64"), DataModel::LP64);
    EXPECT_EQ(defaultDataModel("nitwit64"), DataModel::LP64);
    EXPECT_EQ(defaultDataModel("bin/nitwit32"), DataModel::ILP32);
    EXPECT_EQ(defaultDataModel("nitwit"), DataModel::ILP32);
    EXPECT_EQ(defaultDataModel(""), DataModel::ILP32);
}

TEST(CommandLineTest, OnlyTheNameNitwit64SelectsLp64) {
    EXPECT_EQ(defaultDataModel("nitwit-x86_64"), DataModel::ILP32);
    EXPECT_EQ(defaultDataModel("nitwit_amd64"), DataModel::ILP32);
    EXPECT_EQ(defaultDataModel("/opt/x86_64/nitwit"), DataModel::ILP32);
    EXPECT_EQ(defaultDataModel("/opt/nitwit64/nitwit"), DataModel::ILP32);
    EXPECT_EQ(defaultDataModel("bin/my-nitwit64"), DataModel::ILP32);
}

TEST(CommandLineTest, TheDefaultDataModelAppliesWithoutAFlag) {
    const auto validation = parse({"-w", "w.yml", "prog.c"}, DataModel::LP64);
    ASSERT_TRUE(validation.has_value()) << validation.error();
    ASSERT_TRUE(validation->validation.has_value());
    EXPECT_EQ(validation->validation->dataModel, DataModel::LP64);
    const auto replay = parse({"--replay", "in.txt", "prog.c"}, DataModel::LP64);
    ASSERT_TRUE(replay.has_value()) << replay.error();
    ASSERT_TRUE(replay->replay.has_value());
    EXPECT_EQ(replay->replay->dataModel, DataModel::LP64);
}

TEST(CommandLineTest, AnExplicitDataModelWinsOverTheDefault) {
    const auto ilp32 = parse({"-32", "-w", "w.yml", "prog.c"}, DataModel::LP64);
    ASSERT_TRUE(ilp32.has_value()) << ilp32.error();
    ASSERT_TRUE(ilp32->validation.has_value());
    EXPECT_EQ(ilp32->validation->dataModel, DataModel::ILP32);
    const auto lp64 = parse({"-64", "-w", "w.yml", "prog.c"}, DataModel::ILP32);
    ASSERT_TRUE(lp64.has_value()) << lp64.error();
    ASSERT_TRUE(lp64->validation.has_value());
    EXPECT_EQ(lp64->validation->dataModel, DataModel::LP64);
}

TEST(CommandLineTest, AReplay) {
    const auto parsed = parse({"--replay", "in.txt", "--input-log", "log.txt", "-64", "prog.c"});
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    ASSERT_TRUE(parsed->replay.has_value());
    EXPECT_EQ(parsed->replay->inputs, "in.txt");
    EXPECT_EQ(parsed->replay->inputLog, "log.txt");
    EXPECT_EQ(parsed->replay->program, "prog.c");
    EXPECT_EQ(parsed->replay->dataModel, DataModel::LP64);
    EXPECT_EQ(parsed->replay->terms, ReplayOptions::Terms::None);
}

TEST(CommandLineTest, AReplayThatRecordsTerms) {
    const auto recorded = parse({"--replay", "in.txt", "--terms", "prog.c"});
    ASSERT_TRUE(recorded.has_value()) << recorded.error();
    ASSERT_TRUE(recorded->replay.has_value());
    EXPECT_EQ(recorded->replay->terms, ReplayOptions::Terms::Record);
}

TEST(CommandLineTest, AReplayThatChecksTerms) {
    const auto checked = parse({"--check-terms", "--replay", "in.txt", "prog.c"});
    ASSERT_TRUE(checked.has_value()) << checked.error();
    ASSERT_TRUE(checked->replay.has_value());
    EXPECT_EQ(checked->replay->terms, ReplayOptions::Terms::Check);
}

TEST(CommandLineTest, TermOptionsOnlyForOneReplay) {
    EXPECT_EQ(exitOf({"--terms", "-w", "w.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"--check-terms", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"--replay", "in.txt", "--terms", "--check-terms", "prog.c"}), exitUsage);
}

TEST(CommandLineTest, TheVersion) {
    const auto parsed = parse({"--version"});
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    EXPECT_TRUE(parsed->version);
}

TEST(CommandLineTest, AProgramWithoutWitnessGives240) {
    EXPECT_EQ(exitOf({"prog.c"}), exitNoWitness);
    EXPECT_EQ(exitOf({"-p", "unreach-call.prp", "prog.c"}), exitNoWitness);
}

TEST(CommandLineTest, WrongCommandLinesGive3) {
    EXPECT_EQ(exitOf({}), exitUsage);
    // Both -p and -e.
    EXPECT_EQ(exitOf({"-w", "w.yml", "-p", "unreach-call.prp", "-e", "fail", "prog.c"}), exitUsage);
    // A second program.
    EXPECT_EQ(exitOf({"-w", "w.yml", "prog.c", "other.c"}), exitUsage);
    // -w without a program, or without its value.
    EXPECT_EQ(exitOf({"-w", "w.yml"}), exitUsage);
    EXPECT_EQ(exitOf({"prog.c", "-w"}), exitUsage);
    // Unknown options, replay and validation together, a log without replay.
    EXPECT_EQ(exitOf({"-x", "-w", "w.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"--replay", "in.txt", "-w", "w.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"--input-log", "log.txt", "-w", "w.yml", "prog.c"}), exitUsage);
    // Validation options with a replay; --write-witness without its value.
    EXPECT_EQ(exitOf({"--replay", "in.txt", "--strict-inputs", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"--replay", "in.txt", "--write-witness", "out.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "w.yml", "prog.c", "--write-witness"}), exitUsage);
    // --no-witness-check without --write-witness.
    EXPECT_EQ(exitOf({"-w", "w.yml", "--no-witness-check", "prog.c"}), exitUsage);
}

TEST(CommandLineTest, OptionsGivenTwiceGive3) {
    EXPECT_EQ(exitOf({"-w", "a.yml", "-w", "b.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "a.yml", "--witness", "a.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-32", "-32", "-w", "w.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-32", "-64", "-w", "w.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "w.yml", "-p", "a.prp", "-p", "b.prp", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "w.yml", "-e", "f", "-e", "g", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"--replay", "a.txt", "--replay", "b.txt", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "w.yml", "--strict-inputs", "--strict-inputs", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "w.yml", "--write-witness", "a.yml", "--write-witness", "b.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "w.yml", "--write-witness", "a.yml", "--no-witness-check", "--no-witness-check", "prog.c"}),
              exitUsage);
}

} // namespace
} // namespace nitwit::driver
