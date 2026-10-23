// Runs the nitwit executable on three sets of witnesses and checks its results:
// - testfiles: testfiles/yaml-witnesses and the format 2.x witnesses of testfiles/sv-comp-2019; witnesses named
//   *.invalid must not be confirmed, all others must be;
// - crafted: validation-crafted with the expected verdicts of its *.witness-validation.yml; at least as many right as
//   NITWIT 3.x and no invalid witness confirmed;
// - sample: samples/unreach-call-25-per-producer.tsv; no witness of a correct program confirmed, and at least as many
//   witnesses of buggy programs (without the crafted witnesses of Paulina) confirmed as 3.x. The dataset names programs
//   by hash; they run under their names at SV-COMP, which witnesses use in location.file_name, through symbolic links
//   in the log directory.
// Each run is a process of its own with a timeout of 30 s (as the 3.x results) and a 10 GB address space, at most 8 at
// a time. Every difference to the 3.x results is printed. The dataset is untrusted input: it is only read, and its
// programs only run inside NITWIT.
//
// Usage: corpus_validate <dataset-dir> <repository-root> [testfiles|crafted|sample]...

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <print>
#include <spawn.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

namespace fs = std::filesystem;

constexpr int timeoutSeconds = 30;
constexpr unsigned maxJobs = 8;
constexpr int timeoutExit = 124; // coreutils timeout

struct Job {
    std::string name;
    fs::path witness;
    fs::path program;
    int bits = 32;
    std::string errorFunction = "reach_error";
    bool mustConfirm = false;  // the expected outcome
    std::string producer;      // sample only
    std::optional<int> v3Exit; // the exit code of NITWIT 3.x, if known
};

struct Outcome {
    int exitCode = 0;
    std::string lastLine;
    double seconds = 0;
};

std::string readFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<std::string> split(std::string_view line, char separator) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = line.find(separator, begin);
        fields.emplace_back(line.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
        if (end == std::string_view::npos) {
            return fields;
        }
        begin = end + 1;
    }
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        result.push_back(line);
    }
    return result;
}

/// Runs nitwit on the job through `sh` (for the limits); its output goes to the log file.
Outcome run(const Job& job, const fs::path& logFile) {
    const std::string script = std::format("ulimit -v 10000000; exec timeout -k 5 {} \"$@\"", timeoutSeconds);
    std::vector<std::string> arguments = {"sh",
                                          "-c",
                                          script,
                                          "sh",
                                          NITWIT_EXECUTABLE,
                                          job.bits == 64 ? "-64" : "-32",
                                          "-w",
                                          job.witness.string(),
                                          "-e",
                                          job.errorFunction,
                                          job.program.string()};
    std::vector<char*> argv;
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    const auto start = std::chrono::steady_clock::now();
    pid_t child = 0;
    const int spawned = posix_spawn(&child, "/bin/sh", &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    Outcome outcome;
    if (spawned != 0) {
        outcome.exitCode = -1;
        outcome.lastLine = "cannot start /bin/sh";
        return outcome;
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    outcome.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    outcome.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    for (const std::string& line : lines(readFile(logFile))) {
        if (!line.empty()) {
            outcome.lastLine = line;
        }
    }
    if (outcome.exitCode == timeoutExit) {
        outcome.lastLine = "timeout";
    }
    return outcome;
}

std::vector<Outcome> runAll(const std::vector<Job>& jobs, const fs::path& logDirectory) {
    fs::create_directories(logDirectory);
    std::vector<Outcome> outcomes(jobs.size());
    std::atomic<std::size_t> next = 0;
    std::vector<std::jthread> workers;
    for (unsigned i = 0; i < std::min<std::size_t>(maxJobs, jobs.size()); ++i) {
        workers.emplace_back([&] {
            for (std::size_t job = next++; job < jobs.size(); job = next++) {
                outcomes[job] = run(jobs[job], logDirectory / (jobs[job].name + ".log"));
            }
        });
    }
    workers.clear();
    return outcomes;
}

std::string exitText(std::optional<int> code) {
    return code ? std::to_string(*code) : "-";
}

/// Whether NITWIT 4 exited as 3.x did. 3.x's 252 (the error function called before the target) is 241 in NITWIT 4,
/// with its own result line (docs/exit-codes.md); 241 for leaving the witness is a change.
bool isSameExit(const Outcome& outcome, std::optional<int> v3Exit) {
    if (v3Exit == 252 && outcome.exitCode == 241) {
        return outcome.lastLine.starts_with("FAILED: the target was not reached");
    }
    return outcome.exitCode == v3Exit;
}

/// Prints how the exit codes changed from 3.x, as "3.x -> 4: count".
void printTransitions(const std::vector<Job>& jobs, const std::vector<Outcome>& outcomes) {
    std::map<std::pair<std::string, int>, int> transitions;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        ++transitions[{exitText(jobs[i].v3Exit), outcomes[i].exitCode}];
    }
    std::println("exit codes, 3.x -> 4:");
    for (const auto& [codes, count] : transitions) {
        std::println("  {:>3} -> {:>3}: {}", codes.first, codes.second, count);
    }
}

// ---- testfiles ----

bool checkTestfiles(const fs::path& root, const fs::path& logs) {
    std::vector<Job> jobs;
    const std::pair<std::string_view, std::string_view> directories[] = {{"yaml-witnesses", "reach_error"},
                                                                         {"sv-comp-2019", "__VERIFIER_error"}};
    for (const auto& [directory, errorFunction] : directories) {
        for (const fs::directory_entry& entry : fs::directory_iterator(root / "testfiles" / directory)) {
            const std::string name = entry.path().filename().string();
            const bool isInvalid = name.ends_with(".c.yml.invalid");
            if (!name.ends_with(".c.yml") && !isInvalid) {
                continue;
            }
            // <program>.<producer>.c.yml: the program is <program>.c.
            const std::string stem = name.substr(0, name.find(".c.yml"));
            const fs::path program = entry.path().parent_path() / (stem.substr(0, stem.rfind('.')) + ".c");
            jobs.push_back({.name = name,
                            .witness = entry.path(),
                            .program = program,
                            .bits = 32,
                            .errorFunction = std::string(errorFunction),
                            .mustConfirm = !isInvalid,
                            .producer = "",
                            .v3Exit = std::nullopt});
        }
    }
    std::ranges::sort(jobs, {}, &Job::name);
    const std::vector<Outcome> outcomes = runAll(jobs, logs / "testfiles");
    int wrong = 0;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const bool isConfirmed = outcomes[i].exitCode == 0;
        const bool isRight = isConfirmed == jobs[i].mustConfirm;
        wrong += isRight ? 0 : 1;
        std::println("{} {} {} {}", isRight ? "ok   " : "WRONG", jobs[i].name, outcomes[i].exitCode,
                     outcomes[i].lastLine);
    }
    std::println("testfiles: {} witnesses, {} wrong\n", jobs.size(), wrong);
    return wrong == 0;
}

// ---- validation-crafted ----

/// The value of `key: value` in a line of a validation task, without quotes.
std::optional<std::string> valueOf(const std::string& line, std::string_view key) {
    const std::size_t found = line.find(key);
    if (found == std::string::npos) {
        return std::nullopt;
    }
    std::string value = line.substr(found + key.size());
    std::erase_if(value, [](char c) { return c == '\'' || c == '"' || c == ' '; });
    return value;
}

bool checkCrafted(const fs::path& dataset, const fs::path& logs) {
    const fs::path directory = dataset / "validation-crafted" / "witnesses";
    std::map<std::string, int> v3;
    const fs::path v3Results = dataset / "results" / "2026-10-20-nitwit-strict-v3-validation-crafted" / "results.tsv";
    for (const std::string& line : lines(readFile(v3Results))) {
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.size() == 3) {
            v3[fields[0]] = std::stoi(fields[2]);
        }
    }
    std::vector<Job> jobs;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        const std::string fileName = entry.path().filename().string();
        if (!fileName.ends_with(".witness-validation.yml")) {
            continue;
        }
        // The task: the program among input_files, the witness, the data model, and in `properties:` the expected
        // verdict of unreach-call (false: the witness must be confirmed).
        Job job;
        job.name = fileName.substr(0, fileName.size() - std::string_view(".witness-validation.yml").size());
        bool isProperties = false;
        bool isUnreachCall = false;
        for (const std::string& line : lines(readFile(entry.path()))) {
            if (line.starts_with("properties:")) {
                isProperties = true;
            } else if (!line.starts_with(" ") && !line.empty()) {
                isProperties = false;
            }
            if (isProperties && line.contains("unreach-call.prp")) {
                isUnreachCall = true;
            }
            if (const auto verdict = valueOf(line, "expected_verdict:"); verdict && isProperties) {
                job.mustConfirm = *verdict == "false";
            }
            if (const auto witness = valueOf(line, "witness_input_file:")) {
                job.witness = directory / *witness;
            }
            if (const auto model = valueOf(line, "data_model:")) {
                job.bits = *model == "LP64" ? 64 : 32;
            }
            if (line.contains("- '../") && line.contains(".c'")) {
                job.program = directory / *valueOf(line, "- ");
            }
        }
        if (!isUnreachCall) {
            continue;
        }
        if (const auto found = v3.find(job.name); found != v3.end()) {
            job.v3Exit = found->second;
        }
        jobs.push_back(std::move(job));
    }
    std::ranges::sort(jobs, {}, &Job::name);
    const std::vector<Outcome> outcomes = runAll(jobs, logs / "crafted");
    int right = 0;
    int v3Right = 0;
    int invalidConfirmed = 0;
    int validConfirmed = 0;
    int valid = 0;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const Job& job = jobs[i];
        const bool isConfirmed = outcomes[i].exitCode == 0;
        const bool v3Confirmed = job.v3Exit == 0;
        right += isConfirmed == job.mustConfirm ? 1 : 0;
        v3Right += job.v3Exit && v3Confirmed == job.mustConfirm ? 1 : 0;
        invalidConfirmed += isConfirmed && !job.mustConfirm ? 1 : 0;
        validConfirmed += isConfirmed && job.mustConfirm ? 1 : 0;
        valid += job.mustConfirm ? 1 : 0;
        const std::string_view change = isConfirmed == v3Confirmed ? "" : isConfirmed ? " NEW-CONFIRMED" : " LOST";
        std::println("{} {} must {}: 3.x {} 4 {}{} {}", isConfirmed == job.mustConfirm ? "ok   " : "WRONG", job.name,
                     job.mustConfirm ? "confirm" : "reject ", exitText(job.v3Exit), outcomes[i].exitCode, change,
                     outcomes[i].lastLine);
    }
    printTransitions(jobs, outcomes);
    std::println("validation-crafted: {} of {} right (3.x: {}); valid confirmed {} of {}; invalid confirmed {}\n",
                 right, jobs.size(), v3Right, validConfirmed, valid, invalidConfirmed);
    return invalidConfirmed == 0 && right >= v3Right;
}

// ---- sample ----

/// The file name of the program at SV-COMP: the last component of "programfile" in the witness's metadata
/// (info/<sha256>.json); nullopt if it is missing or not a plain file name (the dataset is untrusted input).
std::optional<std::string> originalProgramName(const fs::path& dataset, const std::string& witness) {
    const bool isHash = witness.size() == 64 && std::ranges::all_of(witness, [](char c) {
                            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                        });
    if (!isHash) {
        return std::nullopt;
    }
    const std::string info = readFile(dataset / "info" / (witness + ".json"));
    constexpr std::string_view key = "\"programfile\": \"";
    const std::size_t begin = info.find(key);
    const std::size_t end = begin == std::string::npos ? begin : info.find('"', begin + key.size());
    if (end == std::string::npos) {
        return std::nullopt;
    }
    const std::string path = info.substr(begin + key.size(), end - begin - key.size());
    std::string name = path.substr(path.rfind('/') + 1);
    const bool isPlain =
        !name.empty() && name.size() <= 200 && name != "." && name != ".." && std::ranges::all_of(name, [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_'
                   || c == '-' || c == '+';
        });
    return isPlain ? std::optional(std::move(name)) : std::nullopt;
}

/// A symbolic link `directory/name` to the program.
fs::path linked(const fs::path& program, const std::string& name, const fs::path& directory) {
    fs::create_directories(directory);
    const fs::path link = directory / name;
    std::error_code ignored;
    fs::remove(link, ignored);
    fs::create_symlink(fs::absolute(program), link);
    return link;
}

bool checkSample(const fs::path& dataset, const fs::path& logs) {
    std::map<std::string, int> v3;
    const fs::path v3Results =
        dataset / "results" / "2026-10-20-nitwit-strict-v3-unreach-call-25-per-producer" / "results.tsv";
    for (const std::string& line : lines(readFile(v3Results))) {
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.size() >= 6 && fields[0] != "witness") {
            v3[fields[0]] = std::stoi(fields[5]);
        }
    }
    std::vector<Job> jobs;
    for (const std::string& line : lines(readFile(dataset / "samples" / "unreach-call-25-per-producer.tsv"))) {
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.size() < 7 || fields[0] == "witness") {
            continue;
        }
        Job job{.name = fields[0],
                .witness = dataset / "witnesses" / "unreach-call" / (fields[0] + ".yml"),
                .program = dataset / fields[1],
                .bits = fields[2] == "64" ? 64 : 32,
                .errorFunction = "reach_error",
                .mustConfirm = fields[3] == "false",
                .producer = fields[4],
                .v3Exit = std::nullopt};
        if (const auto found = v3.find(job.name); found != v3.end()) {
            job.v3Exit = found->second;
        }
        if (const std::optional<std::string> name = originalProgramName(dataset, job.name)) {
            job.program = linked(job.program, *name, logs / "sample-programs" / job.name);
        } else {
            std::println("no original program name for {}; it runs under its hash", job.name);
        }
        jobs.push_back(std::move(job));
    }
    const std::vector<Outcome> outcomes = runAll(jobs, logs / "sample");
    int confirmed = 0;
    int v3Confirmed = 0;
    int buggy = 0;
    int wrongConfirmations = 0;
    double seconds = 0;
    std::map<std::string, std::pair<int, int>> perProducer; // confirmations of buggy programs: 4, 3.x
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const Job& job = jobs[i];
        const bool isConfirmed = outcomes[i].exitCode == 0;
        const bool wasConfirmed = job.v3Exit == 0;
        seconds += outcomes[i].seconds;
        wrongConfirmations += isConfirmed && !job.mustConfirm ? 1 : 0;
        if (job.mustConfirm) {
            perProducer[job.producer].first += isConfirmed ? 1 : 0;
            perProducer[job.producer].second += wasConfirmed ? 1 : 0;
        }
        if (job.mustConfirm && job.producer != "Paulina") {
            ++buggy;
            confirmed += isConfirmed ? 1 : 0;
            v3Confirmed += wasConfirmed ? 1 : 0;
        }
        if (isConfirmed != wasConfirmed || !isSameExit(outcomes[i], job.v3Exit)) {
            const std::string_view change = isConfirmed == wasConfirmed ? "" : isConfirmed ? "NEW-CONFIRMED " : "LOST ";
            std::println("{}{} {} {}-bit buggy={}: 3.x {} 4 {} ({:.1f} s) {}", change, job.name, job.producer, job.bits,
                         job.mustConfirm, exitText(job.v3Exit), outcomes[i].exitCode, outcomes[i].seconds,
                         outcomes[i].lastLine);
        }
    }
    printTransitions(jobs, outcomes);
    std::println("confirmed witnesses of buggy programs per producer, 4 (3.x):");
    for (const auto& [producer, counts] : perProducer) {
        std::println("  {}: {} ({})", producer, counts.first, counts.second);
    }
    std::println(
        "sample: {} of {} buggy programs (without Paulina) confirmed (3.x: {}); {} wrong confirmations; {:.0f} s "
        "in all",
        confirmed, buggy, v3Confirmed, wrongConfirmations, seconds);
    return wrongConfirmations == 0 && confirmed >= v3Confirmed;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::println(stderr, "usage: corpus_validate <dataset-dir> <repository-root> [testfiles|crafted|sample]...");
        return 2;
    }
    const fs::path dataset = argv[1];
    const fs::path root = argv[2];
    const fs::path logs = NITWIT_CORPUS_LOG_DIR;
    std::vector<std::string_view> suites(argv + 3, argv + argc);
    if (suites.empty()) {
        suites = {"testfiles", "crafted", "sample"};
    }
    bool passed = true;
    for (const std::string_view suite : suites) {
        if (suite == "testfiles") {
            passed = checkTestfiles(root, logs) && passed;
        } else if (suite == "crafted") {
            passed = checkCrafted(dataset, logs) && passed;
        } else if (suite == "sample") {
            passed = checkSample(dataset, logs) && passed;
        } else {
            std::println(stderr, "unknown suite {}", suite);
            return 2;
        }
    }
    std::println("{}", passed ? "all criteria met" : "CRITERIA NOT MET");
    return passed ? 0 : 1;
}
