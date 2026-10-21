// layout-probe: checks sema's record layouts, alignments, enumeration values and object sizes against GCC.
// Usage: layout-probe [-32|-64] [--gcc <compiler>] [--jobs <n>] [--work <directory>] [--verbose] <file>...
//
// For each C file, sema's answers are compiled into a probe program: the file itself (its `main` renamed), followed by
// a `main` that compares, at run time, what GCC computes with what sema computed:
// - for every record type that can be named at file scope (tags, typedef names, and `__typeof__` of members for
//   nested unnamed records): sizeof, _Alignof, __alignof__, the offset of every named member (also through anonymous
//   members), and the first bit and width of every bit-field, found by setting it to all ones in a zeroed object;
// - the value and size of every enumeration constant at file scope, and the size and signedness of every enumeration
//   type;
// - sizeof of every object declared at file scope.
// The probe is compiled with `<compiler> -m32|-m64 -w -fpermissive` and run; it prints the mismatches and the number
// of checks. Only the probe's own code runs, so the linker discards unused sections and allows undefined symbols.
// Files that define constructors are skipped, since those would run too.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ast/casting.hpp"
#include "ast/context.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "sema/sema.hpp"
#include "sema/types.hpp"
#include "support/large_stack.hpp"

namespace {

using namespace nitwit;
namespace fs = std::filesystem;

constexpr std::size_t stackBytes = std::size_t{512} << 20;

struct Options {
    DataModel dataModel = DataModel::LP64;
    std::string compiler = "gcc-14";
    unsigned jobs = 4;
    fs::path work = fs::temp_directory_path() / "nitwit-layout-probe";
    bool verbose = false;
    std::vector<std::string> files;
};

struct Counts {
    std::size_t records = 0;
    std::size_t fields = 0;
    std::size_t bitFields = 0;
    std::size_t enumConstants = 0;
    std::size_t enumTypes = 0;
    std::size_t objects = 0;

    Counts& operator+=(const Counts& other) {
        records += other.records;
        fields += other.fields;
        bitFields += other.bitFields;
        enumConstants += other.enumConstants;
        enumTypes += other.enumTypes;
        objects += other.objects;
        return *this;
    }
};

enum class Outcome : std::uint8_t { Checked, Skipped, Failed };

struct FileResult {
    Outcome outcome = Outcome::Skipped;
    std::string reason;
    Counts counts;
    std::size_t checks = 0;
    std::vector<std::string> mismatches;
};

// Writes the C code of the checks and counts them.
class ProbeWriter {
public:
    ProbeWriter(ast::AstContext& context, const ast::TranslationUnit& unit) : context_(context), unit_(unit) {}

    std::string write() {
        for (ast::Decl* decl : unit_.decls) {
            if (const auto* var = ast::dyn_cast<ast::VarDecl>(decl)) {
                lastDeclarations_[var->name().text()] = var; // the composite type of all declarations
            }
        }
        for (ast::Decl* decl : unit_.decls) {
            if (auto* record = ast::dyn_cast<ast::RecordDecl>(decl)) {
                if (!record->name().empty() && record->layout() != nullptr) {
                    probeRecord(record,
                                std::format("{} {}", record->isUnion() ? "union" : "struct", record->name().text()),
                                context_.recordType(record));
                }
            } else if (auto* typedefDecl = ast::dyn_cast<ast::TypedefDecl>(decl)) {
                const ast::QualType type = context_.canonical(typedefDecl->underlying());
                const auto* recordType = ast::dyn_cast<ast::RecordType>(type.type());
                if (recordType != nullptr && recordType->decl()->layout() != nullptr && type.qualifiers().empty()) {
                    probeRecord(recordType->decl(), std::string(typedefDecl->name().text()),
                                context_.typedefType(typedefDecl));
                }
            } else if (auto* enumDecl = ast::dyn_cast<ast::EnumDecl>(decl)) {
                probeEnum(enumDecl);
            } else if (const auto* var = ast::dyn_cast<ast::VarDecl>(decl)) {
                probeObject(var);
            }
        }
        return code_;
    }

    [[nodiscard]] const Counts& counts() const { return counts_; }

private:
    void check(std::string_view what, const std::string& actual, std::uint64_t expected) {
        code_ +=
            std::format("    nitwit_check(\"{}\", (unsigned long long)({}), {}ULL);\n", escape(what), actual, expected);
    }

    static std::string escape(std::string_view text) {
        std::string escaped;
        for (const char c : text) {
            if (c == '"' || c == '\\') {
                escaped += '\\';
            }
            escaped += c;
        }
        return escaped;
    }

    // A record type that `name` denotes; `type` is the type of the name, whose typedef may change the alignment.
    void probeRecord(ast::RecordDecl* record, const std::string& name, ast::QualType type) {
        if (!probed_.insert(record).second) {
            return;
        }
        ++counts_.records;
        const ast::RecordLayout& layout = sema::layoutOf(record);
        // For records, GCC's TYPE_ALIGN (__alignof__) is the ABI alignment (_Alignof).
        const std::uint64_t alignment = sema::alignOf(context_, type);
        check(std::format("sizeof({})", name), std::format("sizeof({})", name), layout.size);
        check(std::format("_Alignof({})", name), std::format("_Alignof({})", name), alignment);
        check(std::format("__alignof__({})", name), std::format("__alignof__({})", name), alignment);
        probeFields(record, name, 0, !context_.canonical(type).qualifiers().isConst);
    }

    // The members of `record`, at `base` bytes in the probed type `name`; anonymous members are entered. Bit-fields
    // are written to find their bits, which needs a `name` that is not const.
    void probeFields(const ast::RecordDecl* record, const std::string& name, std::uint64_t base, bool isWritable) {
        const ast::RecordLayout& layout = sema::layoutOf(record);
        for (const ast::FieldDecl* field : record->fields()) {
            const std::uint64_t offset = base + layout.fieldOffsets[field->index()];
            if (field->isAnonymousRecord()) {
                probeFields(ast::cast<ast::RecordType>(context_.canonical(field->type()).type())->decl(), name, offset,
                            isWritable && !context_.canonical(field->type()).qualifiers().isConst);
                continue;
            }
            if (field->name().empty()) {
                continue;
            }
            const std::string member = std::format("{}.{}", name, field->name().text());
            if (field->isBitField()) {
                if (isWritable) {
                    probeBitField(field, name, offset * 8 + layout.bitOffsets[field->index()]);
                }
                continue;
            }
            ++counts_.fields;
            check(std::format("offsetof({})", member),
                  std::format("__builtin_offsetof({}, {})", name, field->name().text()), offset);
            probeMemberType(field, name);
        }
    }

    void probeBitField(const ast::FieldDecl* field, const std::string& name, std::uint64_t firstBit) {
        if (context_.canonical(field->type()).qualifiers().isConst) {
            return;
        }
        ++counts_.bitFields;
        code_ += std::format("    {{ unsigned char nitwit_buffer[sizeof({0})] __attribute__((aligned(16)));\n"
                             "      __builtin_memset(nitwit_buffer, 0, sizeof nitwit_buffer);\n"
                             "      (({0} *)nitwit_buffer)->{1} = -1;\n"
                             "      nitwit_bits(\"{2}.{1}\", nitwit_buffer, sizeof nitwit_buffer, {3}, {4}); }}\n",
                             name, field->name().text(), escape(name), firstBit, field->bitWidth());
    }

    // A member whose type is (an array of) an unnamed record is probed through __typeof__.
    void probeMemberType(const ast::FieldDecl* field, const std::string& name) {
        ast::QualType type = context_.canonical(field->type());
        std::string access = std::format("((({} *)0)->{})", name, field->name().text());
        while (const auto* array = ast::dyn_cast<ast::ArrayType>(type.type())) {
            type = context_.canonical(array->element());
            access += "[0]";
        }
        if (const auto* record = ast::dyn_cast<ast::RecordType>(type.type());
            record != nullptr && record->decl()->layout() != nullptr && !probed_.contains(record->decl())) {
            probeRecord(record->decl(), std::format("__typeof__({})", access), type);
        }
    }

    void probeEnum(ast::EnumDecl* enumDecl) {
        if (!enumDecl->isComplete() || enumDecl->integerType().isNull()) {
            return;
        }
        if (!enumDecl->name().empty()) {
            ++counts_.enumTypes;
            const std::string name = std::format("enum {}", enumDecl->name().text());
            const ast::QualType type = context_.enumType(enumDecl);
            check(std::format("sizeof({})", name), std::format("sizeof({})", name), sema::sizeOf(context_, type));
            check(std::format("signed({})", name), std::format("(({})-1 < 0)", name),
                  sema::isSignedInteger(context_, type) ? 1 : 0);
        }
        for (const ast::EnumConstantDecl* constant : enumDecl->constants()) {
            ++counts_.enumConstants;
            check(constant->name().text(), std::string(constant->name().text()),
                  static_cast<std::uint64_t>(constant->value()));
            check(std::format("sizeof({})", constant->name().text()),
                  std::format("sizeof({})", constant->name().text()), sema::sizeOf(context_, constant->type()));
        }
    }

    void probeObject(const ast::VarDecl* var) {
        if (var->name().empty() || var->name().text() == "main" || !objects_.insert(var->name().text()).second) {
            return;
        }
        const ast::QualType type = context_.canonical(lastDeclarations_.at(var->name().text())->type());
        if (const auto* array = ast::dyn_cast<ast::ArrayType>(type.type());
            array != nullptr && array->sizeKind() != ast::ArrayType::SizeKind::Constant) {
            return;
        }
        if (sema::isVoid(context_, type)
            || (ast::isa<ast::RecordType>(type.type())
                && ast::cast<ast::RecordType>(type.type())->decl()->layout() == nullptr)) {
            return;
        }
        ++counts_.objects;
        check(std::format("sizeof({})", var->name().text()), std::format("sizeof({})", var->name().text()),
              sema::sizeOf(context_, type));
    }

    ast::AstContext& context_;
    const ast::TranslationUnit& unit_;
    std::string code_;
    Counts counts_;
    std::unordered_set<const ast::RecordDecl*> probed_;
    std::unordered_set<std::string_view> objects_;
    std::unordered_map<std::string_view, const ast::VarDecl*> lastDeclarations_;
};

constexpr std::string_view probeHeader = R"(
#undef main
static int nitwit_mismatches, nitwit_checks;
static void nitwit_check(const char *what, unsigned long long actual, unsigned long long expected) {
    ++nitwit_checks;
    if (actual != expected) {
        ++nitwit_mismatches;
        __builtin_printf("MISMATCH %s: gcc %llu, sema %llu\n", what, actual, expected);
    }
}
static void nitwit_bits(const char *what, const unsigned char *bytes, unsigned long size, unsigned long long first,
                        unsigned long long width) {
    unsigned long long low = ~0ULL, count = 0;
    for (unsigned long i = 0; i < size * 8; ++i) {
        if (bytes[i / 8] >> (i % 8) & 1) {
            if (low == ~0ULL) low = i;
            ++count;
        }
    }
    ++nitwit_checks;
    if (low != first || count != width) {
        ++nitwit_mismatches;
        __builtin_printf("MISMATCH bit-field %s: gcc bit %llu width %llu, sema bit %llu width %llu\n", what, low, count,
                         first, width);
    }
}
int main(void) {
)";

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

FileResult probe(const Options& options, const std::string& file, std::size_t index) {
    FileResult result;
    SourceManager sources;
    StringInterner interner;
    DiagnosticEngine diagnostics(sources);
    const auto loaded = sources.loadFile(file);
    if (!loaded) {
        result.reason = loaded.error().message;
        return result;
    }
    const std::string_view contents = sources.contents(*loaded);
    if (contents.contains("constructor") || contents.contains("init_array")) {
        result.reason = "may define constructors";
        return result;
    }
    const lex::LexResult lexed = lex::lexFile(sources, *loaded, interner, diagnostics);
    if (lexed.needsPreprocessing) {
        result.reason = "needs the preprocessor";
        return result;
    }
    const TargetInfo target = TargetInfo::forDataModel(options.dataModel);
    ast::AstContext context(target, interner);
    ast::TranslationUnit* unit = parse::parseTranslationUnit(lexed.tokens, context, sources, diagnostics);
    if (unit == nullptr || !sema::analyze(*unit, context, diagnostics)) {
        result.reason = "parse or sema error";
        return result;
    }
    ProbeWriter writer(context, *unit);
    const std::string checks = writer.write();
    result.counts = writer.counts();

    const fs::path directory = options.work / std::to_string(index);
    fs::create_directories(directory);
    const fs::path source = directory / "probe.c";
    const fs::path executable = directory / "probe";
    const fs::path output = directory / "output.txt";
    fs::remove(executable);
    {
        std::ofstream out(source);
        out << std::format("#define main nitwit_probe_original_main\n#include \"{}\"\n", fs::absolute(file).string());
        out << probeHeader << checks
            << "    __builtin_printf(\"CHECKS %d MISMATCHES %d\\n\", nitwit_checks, nitwit_mismatches);\n"
               "    return 0;\n}\n";
    }
    const std::string compile =
        std::format("{} {} -w -fpermissive -O0 -no-pie -ffunction-sections -fdata-sections -o '{}' '{}' "
                    "-Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all -Wl,--noinhibit-exec > '{}' 2>&1",
                    options.compiler, options.dataModel == DataModel::ILP32 ? "-m32" : "-m64", executable.string(),
                    source.string(), output.string());
    // Unresolved references from code that never runs make the linker fail, but it still writes the executable.
    if (std::system(compile.c_str()) != 0 && !fs::exists(executable)) {
        result.outcome = Outcome::Failed;
        result.reason = "gcc failed: " + readFile(output).substr(0, 300);
        return result;
    }
    const std::string run = std::format("timeout 60 '{}' > '{}' 2>&1", executable.string(), output.string());
    const int status = std::system(run.c_str());
    const std::string printed = readFile(output);
    std::istringstream lines(printed);
    std::string line;
    bool finished = false;
    while (std::getline(lines, line)) {
        if (line.starts_with("MISMATCH")) {
            result.mismatches.push_back(line);
        } else if (line.starts_with("CHECKS ")) {
            result.checks = std::stoull(line.substr(7));
            finished = true;
        }
    }
    if (status != 0 || !finished) {
        result.outcome = Outcome::Failed;
        result.reason = "the probe did not finish: " + printed.substr(0, 300);
        return result;
    }
    result.outcome = Outcome::Checked;
    if (!options.verbose) {
        fs::remove_all(directory);
    }
    return result;
}

std::optional<Options> parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "-32") {
            options.dataModel = DataModel::ILP32;
        } else if (argument == "-64") {
            options.dataModel = DataModel::LP64;
        } else if (argument == "--gcc" && hasValue) {
            options.compiler = argv[++i];
        } else if (argument == "--jobs" && hasValue) {
            options.jobs = static_cast<unsigned>(std::max(1, std::atoi(argv[++i])));
        } else if (argument == "--work" && hasValue) {
            options.work = argv[++i];
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (!argument.starts_with("-")) {
            options.files.emplace_back(argument);
        } else {
            return std::nullopt;
        }
    }
    if (options.files.empty()) {
        return std::nullopt;
    }
    return options;
}

int run(const Options& options) {
    std::vector<FileResult> results(options.files.size());
    std::atomic<std::size_t> next = 0;
    std::vector<std::jthread> threads;
    for (unsigned t = 0; t < options.jobs; ++t) {
        threads.emplace_back([&] {
            runWithLargeStack(stackBytes, [&] {
                for (std::size_t i = next++; i < options.files.size(); i = next++) {
                    results[i] = probe(options, options.files[i], i);
                }
            });
        });
    }
    threads.clear();

    Counts total;
    std::size_t checked = 0;
    std::size_t skipped = 0;
    std::size_t failed = 0;
    std::size_t checks = 0;
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const FileResult& result = results[i];
        switch (result.outcome) {
        case Outcome::Checked:
            ++checked;
            total += result.counts;
            checks += result.checks;
            mismatches += result.mismatches.size();
            for (const std::string& mismatch : result.mismatches) {
                std::println("{}: {}", options.files[i], mismatch);
            }
            break;
        case Outcome::Skipped:
            ++skipped;
            if (options.verbose) {
                std::println("{}: skipped: {}", options.files[i], result.reason);
            }
            break;
        case Outcome::Failed:
            ++failed;
            std::println("{}: {}", options.files[i], result.reason);
            break;
        }
    }
    std::println("{}: {} files probed, {} skipped, {} failed", options.dataModel == DataModel::ILP32 ? "-m32" : "-m64",
                 checked, skipped, failed);
    std::println("records {}, members {}, bit-fields {}, enumeration types {}, enumeration constants {}, objects {}",
                 total.records, total.fields, total.bitFields, total.enumTypes, total.enumConstants, total.objects);
    std::println("checks {}, mismatches {}", checks, mismatches);
    return mismatches == 0 && failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::optional<Options> options = parseOptions(argc, argv);
    if (!options) {
        std::println(stderr, "usage: layout-probe [-32|-64] [--gcc <compiler>] [--jobs <n>] [--work <directory>] "
                             "[--verbose] <file>...");
        return 2;
    }
    return run(*options);
}
