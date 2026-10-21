// Parses every witness of the SV-COMP 2026 dataset (<dataset-dir>/witnesses/<property>/) and of
// <repository-root>/testfiles and prints counts and the parse errors grouped by message. The witnesses are untrusted
// input and only read. Fails if no witnesses are found or if a test witness that is not marked as invalid (suffix
// ".invalid") does not parse.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "witness/parse.hpp"

namespace nitwit::witness {
namespace {

namespace fs = std::filesystem;

struct Outcome {
    WitnessFormat format = WitnessFormat::Yaml;
    std::optional<std::string> error;
    std::size_t violationSequences = 0; // YAML only
};

Outcome parseFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {.format = WitnessFormat::Yaml, .error = "cannot open the file", .violationSequences = 0};
    }
    const std::string contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    Outcome outcome{.format = detectFormat(contents), .error = std::nullopt, .violationSequences = 0};
    if (outcome.format == WitnessFormat::Yaml) {
        const auto witness = parseYamlWitness(contents);
        if (witness) {
            outcome.violationSequences = witness->violationSequences.size();
        } else {
            outcome.error = witness.error().message;
        }
    } else if (const auto witness = parseGraphmlWitness(contents); !witness) {
        outcome.error = witness.error().message;
    }
    return outcome;
}

/// Parses the files, the small ones on several threads. Parsing needs about 7 times the size of a witness in memory,
/// 1.5 GB for the largest one of 235 MB, and glibc keeps memory freed by a thread in that thread's arena. So the files
/// above `largeFile` bytes are parsed one at a time.
std::vector<Outcome> parseAll(const std::vector<fs::path>& files) {
    constexpr std::uintmax_t largeFile = std::uintmax_t{4} << 20;
    std::vector<Outcome> outcomes(files.size());
    std::vector<std::size_t> smallFiles;
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (fs::file_size(files[i]) > largeFile) {
            outcomes[i] = parseFile(files[i]);
        } else {
            smallFiles.push_back(i);
        }
    }
    std::atomic<std::size_t> next = 0;
    const auto work = [&] {
        for (std::size_t k = next++; k < smallFiles.size(); k = next++) {
            outcomes[smallFiles[k]] = parseFile(files[smallFiles[k]]);
        }
    };
    {
        std::vector<std::jthread> workers;
        // At most 8 threads, which leaves room for builds on the same machine.
        for (unsigned i = 0; i < std::clamp(std::thread::hardware_concurrency(), 1u, 8u); ++i) {
            workers.emplace_back(work);
        }
    } // joins the workers
    return outcomes;
}

/// The message with every number replaced by "N", so that errors that differ only in positions form one group.
std::string groupKey(std::string_view message) {
    std::string key;
    for (std::size_t i = 0; i < message.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(message[i])) == 0) {
            key += message[i];
        } else if (i == 0 || std::isdigit(static_cast<unsigned char>(message[i - 1])) == 0) {
            key += 'N';
        }
    }
    return key;
}

struct ErrorGroup {
    std::size_t count = 0;
    std::string exampleFile;
    std::string exampleMessage;
};

struct Tally {
    std::size_t files = 0;
    std::size_t parsed = 0;
    std::size_t withoutViolationSequence = 0;
    std::size_t withSeveralViolationSequences = 0;
    std::size_t errors = 0;

    void add(const Outcome& outcome) {
        ++files;
        if (outcome.error) {
            ++errors;
            return;
        }
        ++parsed;
        if (outcome.format == WitnessFormat::Yaml && outcome.violationSequences == 0) {
            ++withoutViolationSequence;
        }
        if (outcome.violationSequences > 1) {
            ++withSeveralViolationSequences;
        }
    }
};

void printTallyHeader() {
    std::println("{:<20} {:>8} {:>8} {:>10} {:>10} {:>8}", "", "files", "parsed", "no viol.", "several", "errors");
}

void printTally(std::string_view name, const Tally& tally) {
    std::println("{:<20} {:>8} {:>8} {:>10} {:>10} {:>8}", name, tally.files, tally.parsed,
                 tally.withoutViolationSequence, tally.withSeveralViolationSequences, tally.errors);
}

void printErrorGroups(const std::map<std::string, ErrorGroup>& groups) {
    std::vector<std::pair<std::string, ErrorGroup>> sorted(groups.begin(), groups.end());
    std::ranges::stable_sort(sorted, std::greater{}, [](const auto& group) { return group.second.count; });
    for (const auto& [key, group] : sorted) {
        std::println("{:>8}  {}", group.count, key);
        std::println("          e.g. {}: {}", group.exampleFile, group.exampleMessage);
    }
}

std::vector<fs::path> sortedFiles(const fs::path& directory) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

/// Returns the number of witnesses found.
std::size_t checkDataset(const fs::path& witnessDirectory) {
    std::println("== SV-COMP 2026 witnesses in {}", witnessDirectory.string());
    std::println("(no viol.: parsed without a violation_sequence entry; several: more than one violation_sequence)");
    if (!fs::is_directory(witnessDirectory)) {
        std::println("not a directory");
        return 0;
    }
    const std::vector<fs::path> files = sortedFiles(witnessDirectory);
    const std::vector<Outcome> outcomes = parseAll(files);
    std::map<std::string, Tally> tallies;
    std::map<std::string, std::map<std::string, ErrorGroup>> errorsByProperty;
    Tally total;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const fs::path relative = files[i].lexically_relative(witnessDirectory);
        const std::string property = relative.begin()->string();
        const Outcome& outcome = outcomes[i];
        tallies[property].add(outcome);
        total.add(outcome);
        if (outcome.error) {
            ErrorGroup& group = errorsByProperty[property][groupKey(*outcome.error)];
            if (group.count++ == 0) {
                group.exampleFile = relative.string();
                group.exampleMessage = *outcome.error;
            }
        }
    }
    printTallyHeader();
    for (const auto& [property, tally] : tallies) {
        printTally(property, tally);
    }
    printTally("total", total);
    for (const auto& [property, groups] : errorsByProperty) {
        std::println("-- errors of {}, grouped by message", property);
        printErrorGroups(groups);
    }
    return total.files;
}

bool isTestWitness(const fs::path& file) {
    std::string name = file.filename().string();
    if (name.ends_with(".invalid")) {
        name.resize(name.size() - std::string_view(".invalid").size());
    }
    return name.ends_with(".yml") || name.ends_with(".graphml");
}

/// Returns the number of test witnesses found, or nothing if one that is not marked as invalid does not parse.
std::optional<std::size_t> checkTestFiles(const fs::path& testDirectory) {
    std::println("== test witnesses in {}", testDirectory.string());
    if (!fs::is_directory(testDirectory)) {
        std::println("not a directory");
        return 0;
    }
    std::vector<fs::path> files = sortedFiles(testDirectory);
    std::erase_if(files, [](const fs::path& file) { return !isTestWitness(file); });
    const std::vector<Outcome> outcomes = parseAll(files);
    std::map<std::string, Tally> tallies;
    std::vector<std::string> unexpectedErrors;
    std::vector<std::string> invalidOutcomes;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::string relative = files[i].lexically_relative(testDirectory).string();
        const bool markedInvalid = files[i].extension() == ".invalid";
        const Outcome& outcome = outcomes[i];
        const std::string format = outcome.format == WitnessFormat::Yaml ? "YAML" : "GraphML";
        tallies[std::format("{}{}", format, markedInvalid ? " (.invalid)" : "")].add(outcome);
        const std::string result = outcome.error ? std::format("error: {}", *outcome.error) : "parsed";
        if (markedInvalid) {
            invalidOutcomes.push_back(std::format("{}: {}", relative, result));
        } else if (outcome.error) {
            unexpectedErrors.push_back(std::format("{}: {}", relative, result));
        }
    }
    printTallyHeader();
    for (const auto& [name, tally] : tallies) {
        printTally(name, tally);
    }
    std::println("-- witnesses marked as invalid (they may be well-formed; validation has to reject them)");
    for (const std::string& line : invalidOutcomes) {
        std::println("  {}", line);
    }
    std::println("-- errors in witnesses not marked as invalid: {}", unexpectedErrors.size());
    for (const std::string& line : unexpectedErrors) {
        std::println("  {}", line);
    }
    if (!unexpectedErrors.empty()) {
        return std::nullopt;
    }
    return files.size();
}

int run(const fs::path& datasetDirectory, const fs::path& repositoryRoot) {
    const std::size_t datasetWitnesses = checkDataset(datasetDirectory / "witnesses");
    const std::optional<std::size_t> testWitnesses = checkTestFiles(repositoryRoot / "testfiles");
    if (!testWitnesses) {
        std::println("FAILED: test witnesses that are not marked as invalid do not parse");
        return 1;
    }
    if (datasetWitnesses == 0 || *testWitnesses == 0) {
        std::println("FAILED: no witnesses found");
        return 1;
    }
    return 0;
}

} // namespace
} // namespace nitwit::witness

int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "usage: corpus_witness <dataset-dir> <repository-root>");
        return 2;
    }
    return nitwit::witness::run(argv[1], argv[2]);
}
