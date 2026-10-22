// The command line of nitwit (driver/README.md, "Command line"; docs/exit-codes.md: 3 and 240).

#include "driver/command_line.hpp"

#include <initializer_list>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace nitwit::driver {
namespace {

std::expected<CommandLine, int> parse(std::initializer_list<std::string_view> arguments) {
    const std::vector<std::string_view> list(arguments);
    return parseCommandLine(list);
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

TEST(CommandLineTest, LongOptionsAndTheDefaultDataModel) {
    const auto parsed = parse({"prog.c", "--witness", "w.yml", "--error-function", "fail"});
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    ASSERT_TRUE(parsed->validation.has_value());
    EXPECT_EQ(parsed->validation->errorFunction, "fail");
    EXPECT_EQ(parsed->validation->dataModel, DataModel::ILP32);
}

TEST(CommandLineTest, AReplay) {
    const auto parsed = parse({"--replay", "in.txt", "--input-log", "log.txt", "-64", "prog.c"});
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    ASSERT_TRUE(parsed->replay.has_value());
    EXPECT_EQ(parsed->replay->inputs, "in.txt");
    EXPECT_EQ(parsed->replay->inputLog, "log.txt");
    EXPECT_EQ(parsed->replay->program, "prog.c");
    EXPECT_EQ(parsed->replay->dataModel, DataModel::LP64);
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
}

TEST(CommandLineTest, OptionsGivenTwiceGive3) {
    EXPECT_EQ(exitOf({"-w", "a.yml", "-w", "b.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "a.yml", "--witness", "a.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-32", "-32", "-w", "w.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-32", "-64", "-w", "w.yml", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "w.yml", "-p", "a.prp", "-p", "b.prp", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"-w", "w.yml", "-e", "f", "-e", "g", "prog.c"}), exitUsage);
    EXPECT_EQ(exitOf({"--replay", "a.txt", "--replay", "b.txt", "prog.c"}), exitUsage);
}

} // namespace
} // namespace nitwit::driver
