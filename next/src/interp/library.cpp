#include "interp/library.hpp"

#include <array>
#include <format>
#include <unordered_map>

#include "interp/library_call.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

using library::Call;
using library::entryPoint;
using library::FunctionEntry;
using library::ObjectEntry;

/// Functions whose behaviour the interpreter cannot reproduce: they end the run as Unsupported instead of being
/// answered by the oracle like unknown functions. Threads, non-local jumps, exit handlers, signals, files, processes,
/// and functions that call back into the program.
LibraryResult unsupported(const Call& call) {
    call.unsupported(std::format("{} is not supported", call.name()));
}

constexpr std::array unsupportedNames = {
    "setjmp",  "_setjmp",       "__sigsetjmp", "sigsetjmp",    "longjmp", "_longjmp",  "siglongjmp", "__longjmp_chk",
    "atexit",  "at_quick_exit", "on_exit",     "__cxa_atexit", "signal",  "sigaction", "raise",      "kill",
    "alarm",   "fopen",         "fdopen",      "freopen",      "fclose",  "fseek",     "ftell",      "rewind",
    "tmpfile", "remove",        "rename",      "fork",         "vfork",   "execv",     "execve",     "execvp",
    "execl",   "execlp",        "system",      "popen",        "wait",    "waitpid",   "qsort",      "bsearch",
};
constexpr std::array unsupportedPrefixes = {"pthread_", "thrd_", "mtx_", "cnd_", "tss_"};

using FunctionTable = std::unordered_map<std::string_view, LibraryFunction>;

FunctionTable buildFunctionTable() {
    FunctionTable table;
    const auto add = [&](std::string_view name, LibraryFunction function) {
        const bool added = table.emplace(name, function).second;
        NITWIT_ASSERT(added, "a library function is defined twice");
    };
    for (const auto part : {library::heapFunctions(), library::stringFunctions(), library::stdlibFunctions(),
                            library::stdioFunctions()}) {
        for (const FunctionEntry& entry : part) {
            add(entry.name, entry.function);
        }
    }
    for (const std::string_view name : unsupportedNames) {
        add(name, entryPoint<unsupported>);
    }
    return table;
}

} // namespace

LibraryFunction findLibraryFunction(std::string_view name) {
    static const FunctionTable functions = buildFunctionTable();
    if (const auto found = functions.find(name); found != functions.end()) {
        return found->second;
    }
    // GCC has a __builtin_ form of each library function it knows ("Other Built-in Functions Provided by GCC").
    constexpr std::string_view builtinPrefix = "__builtin_";
    if (name.starts_with(builtinPrefix)) {
        if (const auto found = functions.find(name.substr(builtinPrefix.size())); found != functions.end()) {
            return found->second;
        }
    }
    for (const std::string_view prefix : unsupportedPrefixes) {
        if (name.starts_with(prefix)) {
            return entryPoint<unsupported>;
        }
    }
    return nullptr;
}

std::optional<LibraryObject> libraryObject(std::string_view name, const TargetInfo& target) {
    for (const auto part : {library::stdlibObjects(), library::stdioObjects()}) {
        for (const ObjectEntry& entry : part) {
            if (entry.name == name) {
                return entry.describe(target);
            }
        }
    }
    return std::nullopt;
}

} // namespace nitwit::interp
