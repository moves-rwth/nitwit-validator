// Lexes the programs of the SV-COMP 2026 witness dataset and the C files in testfiles/.
// Usage: corpus_lex <dataset-dir> <repository-root>
//
// For every file it checks that the tokens are ordered and do not overlap, that only whitespace, comments,
// backslash-newlines and the directives the lexer skips (line markers, #line, #ident, #) lie between them, that
// identifiers, keywords, punctuators and pragmas match their spelling, and that every literal decodes (adjacent string
// literals together). It reports the files that need the preprocessor, the pragmas, the warnings and the throughput
// of lexFile. The exit code is 1 if any check fails or any file has a lexer error.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "lex/lexer.hpp"
#include "lex/literals.hpp"

namespace nitwit::lex {
namespace {

namespace fs = std::filesystem;

struct Finding {
    std::string message;
    std::string where;
};

struct FileResult {
    std::size_t bytes = 0;
    std::size_t tokens = 0;
    std::chrono::duration<double> lexTime{};
    bool needsPreprocessing = false;
    std::size_t pragmas = 0;
    std::vector<Finding> lexerErrors;
    std::vector<Finding> lexerWarnings;
    std::vector<Finding> decodeFailures;
    std::vector<Finding> checkFailures; // unexpected text between tokens, overlapping tokens, wrong spellings
};

// Whitespace other than newlines; the lexer also skips null characters.
bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v' || c == '\0';
}

// A backslash, optional blanks and a newline.
std::size_t spliceLength(std::string_view text, std::size_t at) {
    if (!text.substr(at).starts_with('\\')) {
        return 0;
    }
    const std::size_t newline = text.find_first_not_of(" \t\r\f\v", at + 1);
    return newline != std::string_view::npos && text[newline] == '\n' ? newline + 1 - at : 0;
}

// The position of the newline that ends the line containing `at`, following backslash-newlines.
std::size_t endOfLogicalLine(std::string_view text, std::size_t at) {
    std::size_t newline = text.find('\n', at);
    while (newline != std::string_view::npos) {
        const std::size_t lastVisible = text.find_last_not_of(" \t\r\f\v", newline - 1);
        if (lastVisible == std::string_view::npos || lastVisible < at || text[lastVisible] != '\\') {
            break;
        }
        newline = text.find('\n', newline + 1);
    }
    return newline == std::string_view::npos ? text.size() : newline;
}

// The offset after the block comment starting at text[at], whose end may be split by backslash-newlines, or npos.
std::size_t endOfBlockComment(std::string_view text, std::size_t at) {
    for (std::size_t star = text.find('*', at + 2); star != std::string_view::npos; star = text.find('*', star + 1)) {
        std::size_t next = star + 1;
        while (const std::size_t length = spliceLength(text, next)) {
            next += length;
        }
        if (next < text.size() && text[next] == '/') {
            return next + 1;
        }
    }
    return std::string_view::npos;
}

// The name of the directive at text[at] ('#' or '%:'): an identifier, a line number, or empty.
std::string_view directiveName(std::string_view text, std::size_t at) {
    std::size_t i = at + (text[at] == '#' ? 1 : 2);
    while (i < text.size() && isBlank(text[i])) {
        ++i;
    }
    const std::size_t nameStart = i;
    while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_')) {
        ++i;
    }
    return text.substr(nameStart, i - nameStart);
}

// Whether the directive at text[at] is one the lexer skips: a line marker, #line, #ident or the null directive.
bool isSkippedDirective(std::string_view text, std::size_t at) {
    const std::string_view name = directiveName(text, at);
    return name.empty() || std::ranges::all_of(name, [](char c) { return c >= '0' && c <= '9'; }) || name == "ident"
           || name == "line";
}

// An independent scanner for the text between two tokens. Returns the offset of the first byte that is not
// whitespace, a comment, a backslash-newline or a skipped directive. A directive must start a logical line: only a
// newline outside comments and splices starts one.
std::optional<std::size_t> findUnexpectedText(std::string_view text, std::size_t begin, std::size_t end,
                                              bool atLineStart) {
    std::size_t i = begin;
    while (i < end) {
        const std::string_view rest = text.substr(i, end - i);
        if (rest[0] == '\n') {
            atLineStart = true;
            ++i;
        } else if (isBlank(rest[0])) {
            ++i;
        } else if (rest.starts_with("//")) {
            i = endOfLogicalLine(text, i);
        } else if (rest.starts_with("/*")) {
            const std::size_t commentEnd = endOfBlockComment(text, i);
            if (commentEnd == std::string_view::npos || commentEnd > end) {
                return i;
            }
            i = commentEnd;
        } else if (const std::size_t length = spliceLength(text, i); length > 0) {
            i += length;
        } else if (atLineStart && (rest[0] == '#' || rest.starts_with("%:")) && isSkippedDirective(text, i)) {
            i = endOfLogicalLine(text, i);
        } else {
            return i;
        }
    }
    return std::nullopt;
}

bool isDigraphOf(std::string_view text, TokenKind kind) {
    return (text == "<:" && kind == TokenKind::LeftSquare) || (text == ":>" && kind == TokenKind::RightSquare)
           || (text == "<%" && kind == TokenKind::LeftBrace) || (text == "%>" && kind == TokenKind::RightBrace);
}

// An error message if the token does not match its spelling.
std::optional<std::string> checkSpelling(const Token& token, std::string_view text) {
    if (token.is(TokenKind::Identifier)) {
        if (token.identifier.text() != text) {
            return "identifier symbol differs from its spelling";
        }
    } else if (token.is(TokenKind::Pragma)) {
        if (!(text.starts_with('#') || text.starts_with("%:")) || directiveName(text, 0) != "pragma") {
            return "pragma token is not a #pragma directive";
        }
    } else if (isPunctuator(token.kind)) {
        if (text != spelling(token.kind) && !isDigraphOf(text, token.kind)) {
            return "punctuator differs from its spelling";
        }
    } else if (isKeyword(token.kind)) {
        // The other spellings of keywords are GNU's, which all start with two underscores.
        if (text != spelling(token.kind) && !text.starts_with("__")) {
            return "keyword differs from its spelling";
        }
    }
    return std::nullopt;
}

// An error message if the token is a literal that does not decode.
std::optional<std::string> decodeLiteral(const Token& token, std::string_view text) {
    const auto message = [&](const auto& decoded) -> std::optional<std::string> {
        if (decoded) {
            return std::nullopt;
        }
        return std::format("{}: {}", spelling(token.kind), decoded.error().message);
    };
    switch (token.kind) {
    case TokenKind::IntegerLiteral:
        return message(decodeIntegerLiteral(text));
    case TokenKind::FloatingLiteral:
        return message(decodeFloatingLiteral(text));
    case TokenKind::CharacterLiteral:
        return message(decodeCharacterLiteral(text));
    default:
        return std::nullopt;
    }
}

void checkTokens(const SourceManager& sources, FileId file, const LexResult& lexed, FileResult& result) {
    const std::string_view text = sources.contents(file);
    const auto describe = [&](std::size_t offset) {
        return sources.describe({file, static_cast<std::uint32_t>(offset)});
    };
    if (lexed.tokens.empty() || !lexed.tokens.back().is(TokenKind::EndOfFile)) {
        result.checkFailures.push_back({"token vector does not end with EndOfFile", std::string(sources.name(file))});
        return;
    }
    // Adjacent string literals are decoded together, as the parser does.
    std::vector<std::string_view> strings;
    std::size_t stringsStart = 0;
    const auto decodeStrings = [&] {
        if (strings.empty()) {
            return;
        }
        if (const auto decoded = decodeStringLiteral(strings); !decoded) {
            result.decodeFailures.push_back(
                {std::format("{}: {}", spelling(TokenKind::StringLiteral), decoded.error().message),
                 describe(stringsStart)});
        }
        strings.clear();
    };

    std::size_t previousEnd = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    bool atLineStart = true;
    for (const Token& token : lexed.tokens) {
        const std::size_t offset = token.location.offset;
        if (offset < previousEnd || offset + token.length > text.size()) {
            result.checkFailures.push_back({"tokens overlap or lie outside the file", describe(offset)});
            return;
        }
        if (const auto unexpected = findUnexpectedText(text, previousEnd, offset, atLineStart)) {
            result.checkFailures.push_back({"unexpected text between tokens", describe(*unexpected)});
        }
        const std::string_view spelled = text.substr(offset, token.length);
        if (token.is(TokenKind::StringLiteral)) {
            if (strings.empty()) {
                stringsStart = offset;
            }
            strings.push_back(spelled);
        } else {
            decodeStrings();
        }
        if (token.is(TokenKind::EndOfFile)) {
            break;
        }
        if (auto failure = checkSpelling(token, spelled)) {
            result.checkFailures.push_back({std::move(*failure), describe(offset)});
        }
        if (auto failure = decodeLiteral(token, spelled)) {
            result.decodeFailures.push_back({std::move(*failure), describe(offset)});
        }
        previousEnd = offset + token.length;
        atLineStart = false;
    }
    if (!lexed.needsPreprocessing && lexed.tokens.back().location.offset != text.size()) {
        result.checkFailures.push_back({"EndOfFile is not at the end of the file", std::string(sources.name(file))});
    }
}

FileResult lexOne(const fs::path& path) {
    FileResult result;
    SourceManager sources;
    StringInterner interner;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.loadFile(path);
    if (!file) {
        result.checkFailures.push_back({file.error().message, path.string()});
        return result;
    }

    const auto start = std::chrono::steady_clock::now();
    const LexResult lexed = lexFile(sources, *file, interner, diagnostics);
    result.lexTime = std::chrono::steady_clock::now() - start;

    result.bytes = sources.contents(*file).size();
    result.tokens = lexed.tokens.size();
    result.needsPreprocessing = lexed.needsPreprocessing;
    result.pragmas = static_cast<std::size_t>(std::ranges::count(lexed.tokens, TokenKind::Pragma, &Token::kind));
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        auto& findings = diagnostic.severity == Severity::Error ? result.lexerErrors : result.lexerWarnings;
        findings.push_back({diagnostic.message, diagnostics.format(diagnostic)});
    }
    checkTokens(sources, *file, lexed, result);
    return result;
}

// Counts of messages, with the first few places each one occurred.
class MessageCounter {
public:
    void add(const Finding& finding) {
        Entry& entry = entries_[finding.message];
        ++entry.count;
        if (entry.examples.size() < maxExamples) {
            entry.examples.push_back(finding.where);
        }
        ++total_;
    }

    [[nodiscard]] std::size_t total() const { return total_; }

    void print(std::string_view title) const {
        std::println("{}: {}", title, total_);
        std::vector<std::pair<std::string_view, const Entry*>> sorted;
        for (const auto& [message, entry] : entries_) {
            sorted.emplace_back(message, &entry);
        }
        std::ranges::stable_sort(sorted, std::ranges::greater{}, [](const auto& item) { return item.second->count; });
        for (const auto& [message, entry] : sorted) {
            std::println("  {:>8}  {}", entry->count, message);
            for (const std::string& example : entry->examples) {
                std::println("            {}", example);
            }
        }
    }

private:
    static constexpr std::size_t maxExamples = 5;

    struct Entry {
        std::size_t count = 0;
        std::vector<std::string> examples;
    };

    std::map<std::string, Entry> entries_;
    std::size_t total_ = 0;
};

std::vector<fs::path> collectFiles(const fs::path& datasetDir, const fs::path& repositoryRoot) {
    std::vector<fs::path> programs;
    for (const auto& entry : fs::directory_iterator(datasetDir / "programs")) {
        if (entry.is_regular_file() && (entry.path().extension() == ".c" || entry.path().extension() == ".i")) {
            programs.push_back(entry.path());
        }
    }
    std::vector<fs::path> testFiles;
    for (const auto& entry : fs::recursive_directory_iterator(repositoryRoot / "testfiles")) {
        if (entry.is_regular_file() && entry.path().extension() == ".c") {
            testFiles.push_back(entry.path());
        }
    }
    std::ranges::sort(programs);
    std::ranges::sort(testFiles);
    std::println("{} programs in {}, {} C files in {}", programs.size(), (datasetDir / "programs").string(),
                 testFiles.size(), (repositoryRoot / "testfiles").string());
    programs.insert(programs.end(), testFiles.begin(), testFiles.end());
    return programs;
}

// Lexes the files on several threads, mostly to overlap the reading of files; each lexFile call is timed on its own.
std::vector<FileResult> lexAll(const std::vector<fs::path>& files) {
    std::vector<FileResult> results(files.size());
    std::atomic<std::size_t> next = 0;
    const unsigned threadCount = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    std::vector<std::jthread> threads;
    for (unsigned t = 0; t < threadCount; ++t) {
        threads.emplace_back([&] {
            for (std::size_t i = next++; i < files.size(); i = next++) {
                results[i] = lexOne(files[i]);
            }
        });
    }
    threads.clear(); // joins
    return results;
}

int run(const fs::path& datasetDir, const fs::path& repositoryRoot) {
    const auto start = std::chrono::steady_clock::now();
    const std::vector<fs::path> files = collectFiles(datasetDir, repositoryRoot);
    const std::vector<FileResult> results = lexAll(files);
    const std::chrono::duration<double> wallTime = std::chrono::steady_clock::now() - start;

    std::size_t bytes = 0;
    std::size_t tokens = 0;
    double maxTokensPerByte = 0;
    std::chrono::duration<double> lexTime{};
    std::size_t filesWithErrors = 0;
    std::size_t pragmas = 0;
    std::size_t filesWithPragmas = 0;
    std::vector<std::string> needPreprocessing;
    MessageCounter lexerErrors;
    MessageCounter lexerWarnings;
    MessageCounter decodeFailures;
    MessageCounter checkFailures;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const FileResult& result = results[i];
        bytes += result.bytes;
        tokens += result.tokens;
        lexTime += result.lexTime;
        pragmas += result.pragmas;
        if (result.pragmas > 0) {
            ++filesWithPragmas;
        }
        if (result.bytes > 0) {
            maxTokensPerByte =
                std::max(maxTokensPerByte, static_cast<double>(result.tokens) / static_cast<double>(result.bytes));
        }
        if (result.needsPreprocessing) {
            needPreprocessing.push_back(files[i].string());
        }
        if (!result.lexerErrors.empty()) {
            ++filesWithErrors;
        }
        std::ranges::for_each(result.lexerErrors, [&](const Finding& f) { lexerErrors.add(f); });
        std::ranges::for_each(result.lexerWarnings, [&](const Finding& f) { lexerWarnings.add(f); });
        std::ranges::for_each(result.decodeFailures, [&](const Finding& f) { decodeFailures.add(f); });
        std::ranges::for_each(result.checkFailures, [&](const Finding& f) { checkFailures.add(f); });
    }

    const double megabytes = static_cast<double>(bytes) / 1e6;
    std::println("files: {}, {:.1f} MB, {} tokens ({:.3f} per byte on average, at most {:.3f})", files.size(),
                 megabytes, tokens, static_cast<double>(tokens) / std::max(static_cast<double>(bytes), 1.0),
                 maxTokensPerByte);
    std::println("lexFile: {:.2f} s in total, {:.0f} MB/s (wall time with reading and checks: {:.1f} s)",
                 lexTime.count(), megabytes / lexTime.count(), wallTime.count());
    std::println("files needing the preprocessor: {}", needPreprocessing.size());
    for (const std::string& path : needPreprocessing) {
        std::println("  {}", path);
    }
    std::println("pragma tokens: {} in {} files", pragmas, filesWithPragmas);
    std::println("files with lexer errors: {}", filesWithErrors);
    lexerErrors.print("lexer errors");
    lexerWarnings.print("lexer warnings");
    decodeFailures.print("literal decoding failures");
    checkFailures.print("check failures");

    const bool passed =
        !files.empty() && lexerErrors.total() == 0 && decodeFailures.total() == 0 && checkFailures.total() == 0;
    std::println("{}", passed ? "PASSED" : "FAILED");
    return passed ? 0 : 1;
}

} // namespace
} // namespace nitwit::lex

int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "usage: corpus_lex <dataset-dir> <repository-root>");
        return 2;
    }
    return nitwit::lex::run(argv[1], argv[2]);
}
