// The functions of <string.h> (C17 7.24) and their glibc relatives.

#include <algorithm>
#include <limits>
#include <string>

#include "interp/library_call.hpp"

namespace nitwit::interp::library {

namespace {

/// The result of comparing two different bytes as unsigned char (C17 7.24.4p1), as glibc's memcmp, strcmp and
/// strncmp return it: C only fixes the sign. The x86-64 implementations return the difference of the bytes, the
/// i386 ones -1 or 1.
int compareBytes(const Call& call, std::byte first, std::byte second) {
    const int difference = std::to_integer<int>(first) - std::to_integer<int>(second);
    if (call.target().dataModel() == DataModel::LP64) {
        return difference;
    }
    return difference < 0 ? -1 : 1;
}

std::uint64_t stringLength(const Call& call, Address address, std::uint64_t limit) {
    ByteReader reader(call, address);
    std::uint64_t length = 0;
    while (length < limit && reader.next() != std::byte{0}) {
        ++length;
    }
    return length;
}

std::uint64_t stringLength(const Call& call, Address address) {
    return stringLength(call, address, std::numeric_limits<std::uint64_t>::max());
}

// ---- Copying (C17 7.24.2, 7.24.3) ----

LibraryResult memcpy(const Call& call) {
    const Address destination = call.pointerArgument(0);
    const Address source = call.pointerArgument(1);
    const std::uint64_t size = call.sizeArgument(2);
    call.requireDisjoint(destination, size, source, size);
    call.copy(destination, source, size);
    return call.returnPointer(destination);
}

LibraryResult memmove(const Call& call) {
    const Address destination = call.pointerArgument(0);
    call.copy(destination, call.pointerArgument(1), call.sizeArgument(2));
    return call.returnPointer(destination);
}

LibraryResult memset(const Call& call) {
    const Address destination = call.pointerArgument(0);
    const auto byte = static_cast<std::byte>(call.intArgument(1)); // converted to unsigned char (C17 7.24.6.1p2)
    call.fill(destination, byte, call.sizeArgument(2));
    return call.returnPointer(destination);
}

LibraryResult strcpy(const Call& call) {
    const Address destination = call.pointerArgument(0);
    const Address source = call.pointerArgument(1);
    const std::uint64_t size = stringLength(call, source) + 1;
    call.requireDisjoint(destination, size, source, size);
    call.copy(destination, source, size);
    return call.returnPointer(destination);
}

/// Copies at most n characters and pads with null characters up to n (C17 7.24.2.4).
LibraryResult strncpy(const Call& call) {
    const Address destination = call.pointerArgument(0);
    const Address source = call.pointerArgument(1);
    const std::uint64_t size = call.sizeArgument(2);
    const std::uint64_t length = stringLength(call, source, size);
    call.requireDisjoint(destination, size, source, std::min(length + 1, size));
    call.copy(destination, source, length);
    call.fill(destination + length, std::byte{0}, size - length);
    return call.returnPointer(destination);
}

LibraryResult strcat(const Call& call) {
    const Address destination = call.pointerArgument(0);
    const Address source = call.pointerArgument(1);
    const Address end = destination + stringLength(call, destination);
    const std::uint64_t size = stringLength(call, source) + 1;
    call.requireDisjoint(end, size, source, size);
    call.copy(end, source, size);
    return call.returnPointer(destination);
}

/// Appends at most n characters and always a null character (C17 7.24.3.2).
LibraryResult strncat(const Call& call) {
    const Address destination = call.pointerArgument(0);
    const Address source = call.pointerArgument(1);
    const Address end = destination + stringLength(call, destination);
    const std::uint64_t length = stringLength(call, source, call.sizeArgument(2));
    call.requireDisjoint(end, length + 1, source, length);
    call.copy(end, source, length);
    call.fill(end + length, std::byte{0}, 1);
    return call.returnPointer(destination);
}

LibraryResult strdup(const Call& call) {
    const Address source = call.pointerArgument(0);
    const std::uint64_t size = stringLength(call, source) + 1;
    const Address copy = call.allocateHeap(size, false);
    call.copy(copy, source, size);
    return call.returnPointer(copy);
}

LibraryResult strndup(const Call& call) {
    const Address source = call.pointerArgument(0);
    const std::uint64_t length = stringLength(call, source, call.sizeArgument(1));
    const Address copy = call.allocateHeap(length + 1, false);
    call.copy(copy, source, length);
    call.fill(copy + length, std::byte{0}, 1);
    return call.returnPointer(copy);
}

// ---- Comparison (C17 7.24.4) ----

LibraryResult memcmp(const Call& call) {
    ByteReader first(call, call.pointerArgument(0));
    ByteReader second(call, call.pointerArgument(1));
    const std::uint64_t size = call.sizeArgument(2);
    for (std::uint64_t i = 0; i < size; ++i) {
        const std::byte a = first.next();
        const std::byte b = second.next();
        if (a != b) {
            return call.returnInt(compareBytes(call, a, b));
        }
    }
    return call.returnInt(0);
}

/// strcmp and strncmp: compares up to `limit` characters and stops at the first difference or null character.
LibraryResult compareStrings(const Call& call, std::uint64_t limit) {
    ByteReader first(call, call.pointerArgument(0));
    ByteReader second(call, call.pointerArgument(1));
    for (std::uint64_t i = 0; i < limit; ++i) {
        const std::byte a = first.next();
        const std::byte b = second.next();
        if (a != b) {
            return call.returnInt(compareBytes(call, a, b));
        }
        if (a == std::byte{0}) {
            break;
        }
    }
    return call.returnInt(0);
}

LibraryResult strcmp(const Call& call) {
    return compareStrings(call, std::numeric_limits<std::uint64_t>::max());
}

LibraryResult strncmp(const Call& call) {
    return compareStrings(call, call.sizeArgument(2));
}

// ---- Search (C17 7.24.5) ----

LibraryResult memchr(const Call& call) {
    const Address address = call.pointerArgument(0);
    const auto wanted = static_cast<std::byte>(call.intArgument(1));
    const std::uint64_t size = call.sizeArgument(2);
    ByteReader reader(call, address);
    for (std::uint64_t i = 0; i < size; ++i) {
        if (reader.next() == wanted) {
            return call.returnPointer(address + i);
        }
    }
    return call.returnPointer(0);
}

/// The terminating null character is part of the string for strchr and strrchr (C17 7.24.5.2p2).
LibraryResult strchr(const Call& call) {
    const Address address = call.pointerArgument(0);
    const auto wanted = static_cast<std::byte>(call.intArgument(1));
    ByteReader reader(call, address);
    for (Address position = address;; ++position) {
        const std::byte byte = reader.next();
        if (byte == wanted) {
            return call.returnPointer(position);
        }
        if (byte == std::byte{0}) {
            return call.returnPointer(0);
        }
    }
}

LibraryResult strrchr(const Call& call) {
    const Address address = call.pointerArgument(0);
    const auto wanted = static_cast<std::byte>(call.intArgument(1));
    ByteReader reader(call, address);
    Address found = 0;
    for (Address position = address;; ++position) {
        const std::byte byte = reader.next();
        if (byte == wanted) {
            found = position;
        }
        if (byte == std::byte{0}) {
            return call.returnPointer(found);
        }
    }
}

LibraryResult strstr(const Call& call) {
    const Address haystack = call.pointerArgument(0);
    const std::string text = call.readString(haystack);
    const std::size_t position = text.find(call.readString(call.pointerArgument(1)));
    return call.returnPointer(position == std::string::npos ? 0 : haystack + position);
}

LibraryResult strspn(const Call& call) {
    const std::string text = call.readString(call.pointerArgument(0));
    const std::size_t end = text.find_first_not_of(call.readString(call.pointerArgument(1)));
    return call.returnSize(end == std::string::npos ? text.size() : end);
}

LibraryResult strcspn(const Call& call) {
    const std::string text = call.readString(call.pointerArgument(0));
    const std::size_t end = text.find_first_of(call.readString(call.pointerArgument(1)));
    return call.returnSize(end == std::string::npos ? text.size() : end);
}

LibraryResult strpbrk(const Call& call) {
    const Address address = call.pointerArgument(0);
    const std::size_t position = call.readString(address).find_first_of(call.readString(call.pointerArgument(1)));
    return call.returnPointer(position == std::string::npos ? 0 : address + position);
}

LibraryResult strlen(const Call& call) {
    return call.returnSize(stringLength(call, call.pointerArgument(0)));
}

LibraryResult strnlen(const Call& call) {
    return call.returnSize(stringLength(call, call.pointerArgument(0), call.sizeArgument(1)));
}

constexpr FunctionEntry functions[] = {
    {"memcpy", entryPoint<memcpy>},   {"memmove", entryPoint<memmove>}, {"memset", entryPoint<memset>},
    {"memcmp", entryPoint<memcmp>},   {"memchr", entryPoint<memchr>},   {"strlen", entryPoint<strlen>},
    {"strnlen", entryPoint<strnlen>}, {"strcpy", entryPoint<strcpy>},   {"strncpy", entryPoint<strncpy>},
    {"strcat", entryPoint<strcat>},   {"strncat", entryPoint<strncat>}, {"strcmp", entryPoint<strcmp>},
    {"strncmp", entryPoint<strncmp>}, {"strchr", entryPoint<strchr>},   {"strrchr", entryPoint<strrchr>},
    {"strstr", entryPoint<strstr>},   {"strdup", entryPoint<strdup>},   {"strndup", entryPoint<strndup>},
    {"strspn", entryPoint<strspn>},   {"strcspn", entryPoint<strcspn>}, {"strpbrk", entryPoint<strpbrk>},
};

} // namespace

std::span<const FunctionEntry> stringFunctions() {
    return functions;
}

} // namespace nitwit::interp::library
