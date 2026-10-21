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
/// answered by the oracle like unknown functions. Every library function that sema knows is either implemented or
/// listed here (tested), so that none silently becomes an input.
LibraryResult unsupported(const Call& call) {
    call.unsupported(std::format("{} is not supported", call.name()));
}

constexpr std::array unsupportedNames = {
    // Non-local jumps, exit handlers, signals, processes.
    "setjmp",
    "_setjmp",
    "__sigsetjmp",
    "sigsetjmp",
    "longjmp",
    "_longjmp",
    "siglongjmp",
    "__longjmp_chk",
    "atexit",
    "at_quick_exit",
    "on_exit",
    "__cxa_atexit",
    "signal",
    "sigaction",
    "raise",
    "kill",
    "alarm",
    "fork",
    "vfork",
    "execv",
    "execve",
    "execvp",
    "execl",
    "execlp",
    "system",
    "popen",
    "wait",
    "waitpid",
    // Files.
    "fopen",
    "fdopen",
    "freopen",
    "fclose",
    "fseek",
    "ftell",
    "rewind",
    "tmpfile",
    "remove",
    "rename",
    // Input: the values would have to be parsed from inputs the witness does not give.
    "scanf",
    "sscanf",
    "fscanf",
    "vscanf",
    "vsscanf",
    "vfscanf",
    "__isoc99_scanf",
    "__isoc99_sscanf",
    "__isoc99_fscanf",
    "__isoc99_vscanf",
    "__isoc99_vsscanf",
    "__isoc99_vfscanf",
    "__isoc23_scanf",
    "__isoc23_sscanf",
    "__isoc23_fscanf",
    "__isoc23_vscanf",
    "__isoc23_vsscanf",
    "__isoc23_vfscanf",
    // Callbacks into the program.
    "qsort",
    "bsearch",
    // The checking functions of _FORTIFY_SOURCE (also as __builtin___memcpy_chk, ...).
    "__memcpy_chk",
    "__memmove_chk",
    "__memset_chk",
    "__strcpy_chk",
    "__strncpy_chk",
    "__strcat_chk",
    "__sprintf_chk",
    "__snprintf_chk",
    // The machine's stack.
    "__builtin_frame_address",
    "__builtin_return_address",
};
constexpr std::array unsupportedPrefixes = {"pthread_", "thrd_", "mtx_", "cnd_", "tss_"};

using FunctionTable = std::unordered_map<std::string_view, LibraryFunction>;

FunctionTable buildFunctionTable() {
    FunctionTable table;
    const auto add = [&](std::string_view name, LibraryFunction function) {
        const bool added = table.emplace(name, function).second;
        NITWIT_ASSERT(added, "a library function is defined twice");
    };
    for (const auto part :
         {library::heapFunctions(), library::stringFunctions(), library::stdlibFunctions(), library::stdioFunctions(),
          library::ctypeFunctions(), library::mathFunctions(), library::builtinFunctions()}) {
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
    for (const auto part : {library::stdlibObjects(), library::stdioObjects(), library::ctypeObjects()}) {
        for (const ObjectEntry& entry : part) {
            if (entry.name == name) {
                return entry.describe(target);
            }
        }
    }
    return std::nullopt;
}

} // namespace nitwit::interp
