// Parses the programs of the SV-COMP 2026 witness dataset and the C files in testfiles/ that need no preprocessor.
// Usage: corpus_parse <dataset-dir> <repository-root>
//
// Failures are grouped by the message of their first error. For each failure, GCC decides whether the file is valid:
// `gcc-14 -fsyntax-only -w` with -m32 or -m64 as the program's witnesses say (-m32 if none does). GCC 14 rejects
// implicit int and implicit function declarations, which older sv-benchmarks use, so a file it rejects is tried again
// with -fpermissive. The test passes if at least 99.5 % of the files that GCC accepts (either way) parse.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast/context.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "support/large_stack.hpp"

namespace nitwit::parse {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t stackBytes = std::size_t{512} << 20;
constexpr double requiredRate = 0.995;

enum class GccVerdict : std::uint8_t { NotChecked, Accepted, AcceptedPermissive, Rejected };

struct FileResult {
    std::size_t bytes = 0;
    std::chrono::duration<double> parseTime{};
    bool needsPreprocessing = false;
    bool parsed = false;
    std::string message; // the first error
    std::string where;
    GccVerdict gcc = GccVerdict::NotChecked;
};

struct Program {
    fs::path path;
    bool is64Bit = false;
};

// The architecture of each program from the witness index: one JSON object per line with "architecture" and
// "program-sha256". Only these two string fields are read.
std::unordered_map<std::string, bool> readArchitectures(const fs::path& index) {
    std::unordered_map<std::string, bool> is64Bit;
    std::ifstream input(index);
    std::string line;
    const auto field = [&](std::string_view name) -> std::string_view {
        const std::string key = std::format("\"{}\": \"", name);
        const std::size_t start = line.find(key);
        if (start == std::string::npos) {
            return {};
        }
        const std::size_t valueStart = start + key.size();
        const std::size_t end = line.find('"', valueStart);
        return end == std::string::npos ? std::string_view()
                                        : std::string_view(line).substr(valueStart, end - valueStart);
    };
    while (std::getline(input, line)) {
        const std::string_view program = field("program-sha256");
        if (!program.empty()) {
            is64Bit.try_emplace(std::string(program), field("architecture") == "64bit");
        }
    }
    return is64Bit;
}

std::vector<Program> collectPrograms(const fs::path& datasetDir, const fs::path& repositoryRoot) {
    const std::unordered_map<std::string, bool> architectures = readArchitectures(datasetDir / "index.jsonl");
    std::vector<Program> programs;
    for (const auto& entry : fs::directory_iterator(datasetDir / "programs")) {
        const fs::path& path = entry.path();
        if (entry.is_regular_file() && (path.extension() == ".c" || path.extension() == ".i")) {
            const auto found = architectures.find(path.stem().string());
            programs.push_back({path, found != architectures.end() && found->second});
        }
    }
    std::vector<Program> testFiles;
    for (const auto& entry : fs::recursive_directory_iterator(repositoryRoot / "testfiles")) {
        if (entry.is_regular_file() && entry.path().extension() == ".c") {
            testFiles.push_back({entry.path(), false});
        }
    }
    const auto byPath = [](const Program& a, const Program& b) {
        return a.path < b.path;
    };
    std::ranges::sort(programs, byPath);
    std::ranges::sort(testFiles, byPath);
    std::println("{} programs in {}, {} C files in {}", programs.size(), (datasetDir / "programs").string(),
                 testFiles.size(), (repositoryRoot / "testfiles").string());
    programs.insert(programs.end(), testFiles.begin(), testFiles.end());
    return programs;
}

bool gccAccepts(const Program& program, bool permissive) {
    const std::string path = program.path.string();
    if (path.contains('\'')) {
        return false;
    }
    const std::string command = std::format("gcc-14 -fsyntax-only -w {} {} '{}' > /dev/null 2>&1",
                                            program.is64Bit ? "-m64" : "-m32", permissive ? "-fpermissive" : "", path);
    return std::system(command.c_str()) == 0;
}

FileResult parseOne(const Program& program) {
    FileResult result;
    SourceManager sources;
    StringInterner interner;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.loadFile(program.path);
    if (!file) {
        result.message = file.error().message;
        result.where = program.path.string();
        return result;
    }
    result.bytes = sources.contents(*file).size();
    const lex::LexResult lexed = lex::lexFile(sources, *file, interner, diagnostics);
    if (lexed.needsPreprocessing) {
        result.needsPreprocessing = true;
        return result;
    }
    const TargetInfo target = TargetInfo::forDataModel(program.is64Bit ? DataModel::LP64 : DataModel::ILP32);
    ast::AstContext context(target, interner);
    const auto start = std::chrono::steady_clock::now();
    const ast::TranslationUnit* unit = parseTranslationUnit(lexed.tokens, context, sources, diagnostics);
    result.parseTime = std::chrono::steady_clock::now() - start;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.severity == Severity::Error) {
            result.message = diagnostic.message;
            result.where = diagnostics.format(diagnostic);
            break;
        }
    }
    result.parsed = unit != nullptr && !diagnostics.hasErrors();
    if (!result.parsed) {
        result.gcc = gccAccepts(program, false)  ? GccVerdict::Accepted
                     : gccAccepts(program, true) ? GccVerdict::AcceptedPermissive
                                                 : GccVerdict::Rejected;
    }
    return result;
}

// Parses on several threads, each with a large stack for deeply nested programs.
std::vector<FileResult> parseAll(const std::vector<Program>& programs) {
    std::vector<FileResult> results(programs.size());
    std::atomic<std::size_t> next = 0;
    const unsigned threadCount = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    std::vector<std::jthread> threads;
    for (unsigned t = 0; t < threadCount; ++t) {
        threads.emplace_back([&] {
            runWithLargeStack(stackBytes, [&] {
                for (std::size_t i = next++; i < programs.size(); i = next++) {
                    results[i] = parseOne(programs[i]);
                }
            });
        });
    }
    threads.clear(); // joins
    return results;
}

struct Group {
    std::size_t count = 0;
    std::size_t gccAccepts = 0;
    std::vector<std::string> examples;
};

int run(const fs::path& datasetDir, const fs::path& repositoryRoot) {
    const auto start = std::chrono::steady_clock::now();
    const std::vector<Program> programs = collectPrograms(datasetDir, repositoryRoot);
    const std::vector<FileResult> results = parseAll(programs);
    const std::chrono::duration<double> wallTime = std::chrono::steady_clock::now() - start;

    std::size_t parsed = 0;
    std::size_t needPreprocessing = 0;
    std::size_t bytes = 0;
    std::chrono::duration<double> parseTime{};
    std::map<GccVerdict, std::size_t> verdicts;
    std::map<std::string, Group> groups;
    for (const FileResult& result : results) {
        if (result.needsPreprocessing) {
            ++needPreprocessing;
            continue;
        }
        bytes += result.bytes;
        parseTime += result.parseTime;
        if (result.parsed) {
            ++parsed;
            continue;
        }
        ++verdicts[result.gcc];
        Group& group = groups[result.message];
        ++group.count;
        if (result.gcc != GccVerdict::Rejected) {
            ++group.gccAccepts;
        }
        if (group.examples.size() < 5) {
            group.examples.push_back(result.where);
        }
    }

    const std::size_t failed = results.size() - needPreprocessing - parsed;
    const std::size_t validFailures = verdicts[GccVerdict::Accepted] + verdicts[GccVerdict::AcceptedPermissive];
    const double rate =
        static_cast<double>(parsed) / static_cast<double>(std::max<std::size_t>(parsed + validFailures, 1));
    std::println("files: {}, needing the preprocessor (skipped): {}, parsed: {}, failed: {}", results.size(),
                 needPreprocessing, parsed, failed);
    std::println("parseTranslationUnit: {:.2f} s for {:.1f} MB ({:.0f} MB/s); wall time {:.1f} s", parseTime.count(),
                 static_cast<double>(bytes) / 1e6, static_cast<double>(bytes) / 1e6 / std::max(parseTime.count(), 1e-9),
                 wallTime.count());
    std::println("failures that gcc accepts: {} (with -fpermissive only: {}); gcc rejects: {}",
                 verdicts[GccVerdict::Accepted] + verdicts[GccVerdict::AcceptedPermissive],
                 verdicts[GccVerdict::AcceptedPermissive], verdicts[GccVerdict::Rejected]);
    std::println("parsed {:.3f} % of the files gcc accepts (required: {:.1f} %)", 100 * rate, 100 * requiredRate);

    std::vector<std::pair<const std::string*, const Group*>> sorted;
    for (const auto& [message, group] : groups) {
        sorted.emplace_back(&message, &group);
    }
    std::ranges::stable_sort(sorted, std::ranges::greater{}, [](const auto& item) { return item.second->count; });
    std::println("failures by first error ({} messages; count, of which gcc accepts):", sorted.size());
    for (const auto& [message, group] : sorted) {
        std::println("  {:>6} {:>6}  {}", group->count, group->gccAccepts, *message);
        for (const std::string& example : group->examples) {
            std::println("                 {}", example);
        }
    }

    const bool passed = parsed > 0 && rate >= requiredRate;
    std::println("{}", passed ? "PASSED" : "FAILED");
    return passed ? 0 : 1;
}

} // namespace
} // namespace nitwit::parse

int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "usage: corpus_parse <dataset-dir> <repository-root>");
        return 2;
    }
    return nitwit::parse::run(argv[1], argv[2]);
}
