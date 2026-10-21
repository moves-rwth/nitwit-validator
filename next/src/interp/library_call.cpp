#include "interp/library_call.hpp"

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

#include "support/assert.hpp"
#include "value/arithmetic.hpp"

namespace nitwit::interp::library {

namespace {

value::FloatFormat floatFormatOf(value::Value::Kind kind) {
    switch (kind) {
    case value::Value::Kind::Float:
        return value::FloatFormat::Float;
    case value::Value::Kind::Double:
        return value::FloatFormat::Double;
    default:
        return value::FloatFormat::LongDouble;
    }
}

} // namespace

LibraryResult ending(LibraryResult::Kind kind, std::string message) {
    LibraryResult result;
    result.kind = kind;
    result.message = std::move(message);
    return result;
}

value::IntegerFormat Call::format(ScalarType type) const {
    NITWIT_ASSERT(type != ScalarType::Float && type != ScalarType::Double && type != ScalarType::LongDouble,
                  "an integer format needs an integer or pointer type");
    if (type == ScalarType::Pointer) {
        return {.bits = static_cast<std::uint8_t>(target().pointerBits()), .isSigned = false};
    }
    return {.bits = static_cast<std::uint8_t>(target().layout(type).size * 8), .isSigned = target().isSigned(type)};
}

// ---- Arguments ----

const value::Value& Call::argument(std::size_t index) const {
    if (index >= call_.arguments.size()) {
        undefinedBehavior(std::format("{} is called with too few arguments", name()));
    }
    return call_.arguments[index];
}

UInt128 Call::integerArgument(std::size_t index, ScalarType type) const {
    const value::Value& argument = this->argument(index);
    if (argument.kind() != value::Value::Kind::Integer) {
        undefinedBehavior(std::format("argument {} of {} is not an integer or a pointer", index + 1, name()));
    }
    return value::convertInteger(argument, format(type)).bits();
}

Int128 Call::signedArgument(std::size_t index, ScalarType type) const {
    return static_cast<Int128>(integerArgument(index, type));
}

int Call::intArgument(std::size_t index) const {
    return static_cast<int>(signedArgument(index, ScalarType::Int));
}

std::uint64_t Call::sizeArgument(std::size_t index) const {
    return static_cast<std::uint64_t>(integerArgument(index, target().sizeType()));
}

Address Call::pointerArgument(std::size_t index) const {
    return static_cast<Address>(integerArgument(index, ScalarType::Pointer));
}

value::Value Call::floatArgument(std::size_t index, value::FloatFormat format) const {
    const value::Value& argument = this->argument(index);
    const value::Value::Kind kind = argument.kind();
    if (kind != value::Value::Kind::Float && kind != value::Value::Kind::Double
        && kind != value::Value::Kind::LongDouble) {
        undefinedBehavior(std::format("argument {} of {} is not a floating-point number", index + 1, name()));
    }
    return value::convertFloat(argument, floatFormatOf(kind), format);
}

// ---- Results ----

LibraryResult Call::returnInteger(UInt128 bits, ScalarType type) const {
    return LibraryResult::returning(value::Value::integer(bits, format(type)));
}

LibraryResult Call::returnInt(int value) const {
    return returnInteger(static_cast<UInt128>(Int128{value}), ScalarType::Int);
}

LibraryResult Call::returnSize(std::uint64_t value) const {
    return returnInteger(value, target().sizeType());
}

LibraryResult Call::returnPointer(Address address) const {
    return returnInteger(address, ScalarType::Pointer);
}

// ---- Ending the call otherwise ----

void Call::undefinedBehavior(std::string message) const {
    throw Stop{ending(LibraryResult::Kind::UndefinedBehavior, std::move(message))};
}

void Call::unsupported(std::string message) const {
    throw Stop{ending(LibraryResult::Kind::Unsupported, std::move(message))};
}

void Call::memoryError(bool isWrite, const MemoryError& error) const {
    const std::string_view access = isWrite ? "writes" : "reads";
    switch (error.kind) {
    case MemoryErrorKind::NullPointer:
        undefinedBehavior(std::format("{} {} through a null pointer ({:#x})", name(), access, error.address));
    case MemoryErrorKind::InvalidAddress:
        undefinedBehavior(std::format("{} {} at {:#x}, which is not in any object", name(), access, error.address));
    case MemoryErrorKind::OutOfBounds:
        undefinedBehavior(std::format("{} {} past the end of the object ({} bytes at {:#x})", name(), access,
                                      error.size, error.address));
    case MemoryErrorKind::DeadObject:
        undefinedBehavior(
            std::format("{} {} an object whose lifetime has ended, at {:#x}", name(), access, error.address));
    case MemoryErrorKind::ReadOnlyWrite:
        undefinedBehavior(std::format("{} writes to read-only memory at {:#x}", name(), error.address));
    case MemoryErrorKind::CodeAccess:
        undefinedBehavior(std::format("{} {} the code of a function at {:#x}", name(), access, error.address));
    case MemoryErrorKind::InvalidFree:
    case MemoryErrorKind::OutOfAddressSpace:
        break;
    }
    NITWIT_UNREACHABLE("reads and writes report no other memory errors");
}

// ---- Memory ----

void Call::read(Address address, std::span<std::byte> out) const {
    if (const auto result = memory().read(address, out); !result) {
        memoryError(false, result.error());
    }
}

std::vector<std::byte> Call::readBytes(Address address, std::uint64_t size) const {
    const std::optional<AllocationInfo> object = memory().find(address);
    const std::uint64_t available =
        object && object->isAlive && address < object->base + object->size ? object->base + object->size - address : 0;
    if (size > available) {
        // Reading one byte more than the object has left reports the error without allocating `size` bytes.
        std::vector<std::byte> tooMany(available + 1);
        read(address, tooMany);
        NITWIT_UNREACHABLE("a read beyond the object must fail");
    }
    std::vector<std::byte> bytes(size);
    read(address, bytes);
    return bytes;
}

void Call::write(Address address, std::span<const std::byte> bytes) const {
    if (const auto result = memory().write(address, bytes); !result) {
        memoryError(true, result.error());
    }
}

void Call::copy(Address destination, Address source, std::uint64_t size) const {
    if (const auto result = memory().copy(destination, source, size); !result) {
        // Memory::copy checks the source first, so an error at the source address is a read error, unless it is
        // the write of an overlapping destination into read-only memory.
        const MemoryError& error = result.error();
        memoryError(error.address != source || error.kind == MemoryErrorKind::ReadOnlyWrite, error);
    }
}

void Call::fill(Address address, std::byte value, std::uint64_t size) const {
    if (const auto result = memory().fill(address, value, size); !result) {
        memoryError(true, result.error());
    }
}

UInt128 Call::readInteger(Address address, ScalarType type) const {
    const value::IntegerFormat integerFormat = format(type);
    std::array<std::byte, 16> bytes{};
    const std::span<std::byte> used = std::span(bytes).first(integerFormat.bits / 8u);
    read(address, used);
    return value::decodeInteger(used, integerFormat).bits();
}

void Call::writeInteger(Address address, UInt128 bits, ScalarType type) const {
    const value::IntegerFormat integerFormat = format(type);
    std::array<std::byte, 16> bytes{};
    const std::span<std::byte> used = std::span(bytes).first(integerFormat.bits / 8u);
    value::encodeInteger(value::Value::integer(bits, integerFormat), integerFormat, used);
    write(address, used);
}

std::string Call::readString(Address address, std::uint64_t limit) const {
    std::string text;
    ByteReader reader(*this, address);
    while (text.size() < limit) {
        const std::byte byte = reader.next();
        if (byte == std::byte{0}) {
            break;
        }
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

void Call::requireDisjoint(Address first, std::uint64_t firstSize, Address second, std::uint64_t secondSize) const {
    const bool overlap = firstSize > 0 && secondSize > 0 && UInt128{first} < UInt128{second} + secondSize
                         && UInt128{second} < UInt128{first} + firstSize;
    if (overlap) {
        undefinedBehavior(std::format("{} copies between overlapping objects", name()));
    }
}

Address Call::allocateHeap(std::uint64_t size, bool zeroed) const {
    const auto address = context_.allocateHeap(size, zeroed);
    if (!address) {
        throw Stop{ending(LibraryResult::Kind::OutOfMemory, std::format("{} cannot allocate {} bytes", name(), size))};
    }
    return *address;
}

void Call::setErrno(int value) const {
    writeInteger(objectAddress("errno"), static_cast<UInt128>(Int128{value}), ScalarType::Int);
}

void initializeObject(LibraryContext& context, Address address, std::span<const std::byte> bytes) {
    const auto written = context.memory().write(address, bytes);
    NITWIT_ASSERT(written.has_value(), "library objects must be allocated in writable memory");
}

void initializeZero(LibraryContext& context, Address address) {
    const std::optional<AllocationInfo> object = context.memory().find(address);
    NITWIT_ASSERT(object && object->base == address, "initializeZero needs the address of a library object");
    const auto filled = context.memory().fill(address, std::byte{0}, object->size);
    NITWIT_ASSERT(filled.has_value(), "library objects must be allocated in writable memory");
}

void initializePointer(LibraryContext& context, Address address, Address value) {
    const value::IntegerFormat format{.bits = static_cast<std::uint8_t>(context.target().pointerBits()),
                                      .isSigned = false};
    std::array<std::byte, 8> bytes{};
    const std::span<std::byte> used = std::span(bytes).first(format.bits / 8u);
    value::encodeInteger(value::Value::integer(value, format), format, used);
    initializeObject(context, address, used);
}

// ---- ByteReader ----

std::byte ByteReader::next() {
    if (index_ == filled_) {
        // Fetch up to blockSize bytes, but none beyond the object that contains position_. Where no live object
        // does, a one-byte read reports the error.
        std::uint64_t size = 1;
        if (const std::optional<AllocationInfo> object = call_.memory().find(position_);
            object && object->isAlive && position_ < object->base + object->size) {
            size = std::min<std::uint64_t>(object->base + object->size - position_, blockSize);
        }
        call_.read(position_, std::span(block_).first(size));
        index_ = 0;
        filled_ = size;
    }
    ++position_;
    return block_[index_++];
}

} // namespace nitwit::interp::library
