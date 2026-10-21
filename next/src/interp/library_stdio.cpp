// Output through <stdio.h> (C17 7.21) to stdout and stderr, the only output streams the interpreter has. stdout goes
// to the context's output stream, unbuffered; stderr is discarded.

#include <algorithm>
#include <array>
#include <climits>
#include <format>
#include <limits>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

#include "interp/library_call.hpp"
#include "interp/library_format.hpp"

namespace nitwit::interp::library {

namespace {

// glibc's stdin, stdout and stderr are pointers to the FILE objects _IO_2_1_stdin_, ... Their contents do not
// matter here: the library recognizes the streams by their addresses.
constexpr std::string_view stdinFile = "_IO_2_1_stdin_";
constexpr std::string_view stdoutFile = "_IO_2_1_stdout_";
constexpr std::string_view stderrFile = "_IO_2_1_stderr_";

enum class Stream : std::uint8_t { Stdin, Stdout, Stderr };

/// The stream that the FILE* argument at `index` designates.
Stream streamArgument(const Call& call, std::size_t index) {
    const Address stream = call.pointerArgument(index);
    if (stream == call.objectAddress(stdoutFile)) {
        return Stream::Stdout;
    }
    if (stream == call.objectAddress(stderrFile)) {
        return Stream::Stderr;
    }
    if (stream == call.objectAddress(stdinFile)) {
        return Stream::Stdin;
    }
    if (stream == 0) {
        call.undefinedBehavior(std::format("{} uses a null stream", call.name()));
    }
    call.unsupported(std::format("{} uses a stream other than stdin, stdout and stderr", call.name()));
}

/// The stream an output function writes to: stdout or stderr.
Stream outputStream(const Call& call, std::size_t index) {
    const Stream stream = streamArgument(call, index);
    if (stream == Stream::Stdin) {
        call.unsupported(std::format("{} writes to stdin", call.name()));
    }
    return stream;
}

void emit(const Call& call, Stream stream, std::string_view text) {
    std::ostream* output = call.context().output();
    if (stream == Stream::Stdout && output != nullptr) {
        output->write(text.data(), static_cast<std::streamsize>(text.size()));
    }
}

/// The int that the printf family returns: the number of characters.
LibraryResult returnCount(const Call& call, std::size_t count) {
    // The formatter's output limit keeps the count far below INT_MAX.
    return call.returnInt(static_cast<int>(count));
}

// ---- The printf family; the v forms get the arguments of their va_list (LibraryCall) ----

LibraryResult printf(const Call& call) {
    const std::string text = formatPrintf(call, call.pointerArgument(0), 1).text;
    emit(call, Stream::Stdout, text);
    return returnCount(call, text.size());
}

LibraryResult fprintf(const Call& call) {
    const Stream stream = outputStream(call, 0);
    const std::string text = formatPrintf(call, call.pointerArgument(1), 2).text;
    emit(call, stream, text);
    return returnCount(call, text.size());
}

/// Writes the first `length` characters of the output of sprintf or snprintf and a null character to `buffer`, which
/// must not overlap what the call read: copying between overlapping objects is undefined (C17 7.21.6.5p2,
/// 7.21.6.6p2).
void writeOutput(const Call& call, Address buffer, const Formatted& formatted, std::size_t length) {
    for (const MemoryRange& read : formatted.reads) {
        call.requireDisjoint(buffer, length + 1, read.address, read.size);
    }
    std::string bytes = formatted.text.substr(0, length);
    bytes.push_back('\0');
    call.write(buffer, std::as_bytes(std::span(bytes)));
}

LibraryResult sprintf(const Call& call) {
    const Address buffer = call.pointerArgument(0);
    const Formatted formatted = formatPrintf(call, call.pointerArgument(1), 2);
    writeOutput(call, buffer, formatted, formatted.text.size());
    return returnCount(call, formatted.text.size());
}

/// Writes at most n - 1 characters and a null character; returns the length of the whole output (C17 7.21.6.5).
LibraryResult snprintf(const Call& call) {
    const Address buffer = call.pointerArgument(0);
    const std::uint64_t size = call.sizeArgument(1);
    const Formatted formatted = formatPrintf(call, call.pointerArgument(2), 3);
    if (size > 0) {
        writeOutput(call, buffer, formatted, std::min<std::uint64_t>(formatted.text.size(), size - 1));
    }
    return returnCount(call, formatted.text.size());
}

/// asprintf(&pointer, format, ...): the output in a new heap block (a GNU extension).
LibraryResult asprintf(const Call& call) {
    const Address pointer = call.pointerArgument(0);
    std::string text = formatPrintf(call, call.pointerArgument(1), 2).text;
    const std::size_t count = text.size();
    text.push_back('\0');
    const Address block = call.allocateHeap(text.size(), false);
    call.write(block, std::as_bytes(std::span(text)));
    call.writeInteger(pointer, block, ScalarType::Pointer);
    return returnCount(call, count);
}

// ---- Characters and strings ----

/// glibc's puts returns the number of characters written, including the newline.
LibraryResult puts(const Call& call) {
    const std::string text = call.readString(call.pointerArgument(0)) + '\n';
    emit(call, Stream::Stdout, text);
    return returnCount(call, std::min<std::size_t>(text.size(), INT_MAX));
}

/// glibc's fputs returns 1.
LibraryResult fputs(const Call& call) {
    const std::string text = call.readString(call.pointerArgument(0));
    emit(call, outputStream(call, 1), text);
    return call.returnInt(1);
}

/// putchar, fputc and putc write and return the argument converted to unsigned char (C17 7.21.7.3).
LibraryResult writeCharacter(const Call& call, Stream stream) {
    const auto character = static_cast<unsigned char>(call.intArgument(0));
    emit(call, stream, std::string(1, static_cast<char>(character)));
    return call.returnInt(character);
}

LibraryResult putchar(const Call& call) {
    return writeCharacter(call, Stream::Stdout);
}

LibraryResult fputc(const Call& call) {
    return writeCharacter(call, outputStream(call, 1));
}

/// fwrite(pointer, size, count, stream) returns the number of elements written; 0 if size or count is 0.
LibraryResult fwrite(const Call& call) {
    const Address source = call.pointerArgument(0);
    const UInt128 total = UInt128{call.sizeArgument(1)} * call.sizeArgument(2);
    const Stream stream = outputStream(call, 3);
    if (total > std::numeric_limits<std::uint64_t>::max()) {
        call.undefinedBehavior(std::format("{} reads past the end of the object", call.name()));
    }
    const auto size = static_cast<std::uint64_t>(total);
    call.requireReadable(source, size);
    // In blocks, so that a large range needs no host memory of its size.
    std::array<std::byte, 4096> block{};
    for (std::uint64_t done = 0; done < size;) {
        const std::span<std::byte> part = std::span(block).first(std::min<std::uint64_t>(block.size(), size - done));
        call.read(source + done, part);
        emit(call, stream, std::string_view(reinterpret_cast<const char*>(part.data()), part.size()));
        done += part.size();
    }
    return call.returnSize(size == 0 ? 0 : call.sizeArgument(2));
}

/// fflush(NULL) flushes all streams; the interpreter's are unbuffered.
LibraryResult fflush(const Call& call) {
    if (call.pointerArgument(0) != 0) {
        streamArgument(call, 0); // glibc's fflush(stdin) succeeds as well
    }
    return call.returnInt(0);
}

// ---- The objects ----

LibraryObject fileObject(const TargetInfo& target) {
    // sizeof(FILE) in glibc
    const std::uint64_t size = target.dataModel() == DataModel::LP64 ? 216 : 148;
    const std::uint64_t alignment = target.layout(ScalarType::Pointer).align;
    return {.size = size, .alignment = alignment, .initialize = initializeZero};
}

/// The pointer variable `stream` (stdin, ...), pointing to the FILE object `file`.
template <const std::string_view& file>
LibraryObject streamObject(const TargetInfo& target) {
    const std::uint64_t size = target.layout(ScalarType::Pointer).size;
    return {.size = size, .alignment = size, .initialize = [](LibraryContext& context, Address address) {
                initializePointer(context, address, context.objectAddress(file));
            }};
}

constexpr FunctionEntry functions[] = {
    {"printf", entryPoint<printf>},
    {"vprintf", entryPoint<printf>},
    {"fprintf", entryPoint<fprintf>},
    {"vfprintf", entryPoint<fprintf>},
    {"sprintf", entryPoint<sprintf>},
    {"vsprintf", entryPoint<sprintf>},
    {"snprintf", entryPoint<snprintf>},
    {"vsnprintf", entryPoint<snprintf>},
    {"asprintf", entryPoint<asprintf>},
    {"vasprintf", entryPoint<asprintf>},
    {"puts", entryPoint<puts>},
    {"fputs", entryPoint<fputs>},
    {"fputs_unlocked", entryPoint<fputs>},
    {"putchar", entryPoint<putchar>},
    {"putchar_unlocked", entryPoint<putchar>},
    {"fputc", entryPoint<fputc>},
    {"fputc_unlocked", entryPoint<fputc>},
    {"putc", entryPoint<fputc>},
    {"putc_unlocked", entryPoint<fputc>},
    {"fwrite", entryPoint<fwrite>},
    {"fwrite_unlocked", entryPoint<fwrite>},
    {"fflush", entryPoint<fflush>},
    {"fflush_unlocked", entryPoint<fflush>},
};

constexpr ObjectEntry objects[] = {
    {"stdin", streamObject<stdinFile>},
    {"stdout", streamObject<stdoutFile>},
    {"stderr", streamObject<stderrFile>},
    {stdinFile, fileObject},
    {stdoutFile, fileObject},
    {stderrFile, fileObject},
};

} // namespace

std::span<const FunctionEntry> stdioFunctions() {
    return functions;
}

std::span<const ObjectEntry> stdioObjects() {
    return objects;
}

} // namespace nitwit::interp::library
