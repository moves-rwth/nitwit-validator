#pragma once

// For the library tests: a LibraryContext over a real Memory, and a fixture, parameterized by the data model, that
// builds arguments, calls library functions and inspects their results.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "interp/library.hpp"
#include "interp/memory.hpp"
#include "support/int128.hpp"
#include "support/target.hpp"
#include "value/value.hpp"

namespace nitwit::interp::test {

/// Library objects are allocated on first use in the Global region; heap blocks count against `heapLimit`.
class TestContext : public LibraryContext {
public:
    explicit TestContext(DataModel model) : target_(TargetInfo::forDataModel(model)), memory_(target_.pointerBits()) {}

    Memory& memory() override { return memory_; }
    [[nodiscard]] const TargetInfo& target() const override { return target_; }
    std::ostream* output() override { return &output_; }

    std::expected<Address, MemoryError> allocateHeap(std::uint64_t size, bool zeroed) override {
        if (memory_.liveBytes() + size > heapLimit) {
            return std::unexpected(MemoryError{.kind = MemoryErrorKind::OutOfAddressSpace, .address = 0, .size = size});
        }
        return zeroed ? memory_.allocateZeroed(Region::Heap, size, target_.maxAlign(), "malloc")
                      : memory_.allocate(Region::Heap, size, target_.maxAlign(), "malloc");
    }

    Address objectAddress(std::string_view name) override {
        if (const auto found = objects_.find(name); found != objects_.end()) {
            return found->second;
        }
        const std::optional<LibraryObject> object = libraryObject(name, target_);
        if (!object) {
            ADD_FAILURE() << "the library has no object " << name;
            return 0;
        }
        // The map's key outlives the Memory, as the name of an allocation must.
        const auto entry = objects_.emplace(std::string(name), 0).first;
        entry->second = memory_.allocate(Region::Global, object->size, object->alignment, entry->first).value();
        if (object->initialize != nullptr) {
            object->initialize(*this, entry->second); // may allocate other objects
        }
        return entry->second;
    }

    /// What the program wrote to stdout.
    [[nodiscard]] std::string printed() const { return output_.str(); }

    std::uint64_t heapLimit = std::uint64_t{1} << 30;

private:
    TargetInfo target_;
    std::map<std::string, Address, std::less<>> objects_;
    Memory memory_;
    std::ostringstream output_;
};

inline std::string modelName(const ::testing::TestParamInfo<DataModel>& model) {
    return model.param == DataModel::LP64 ? "LP64" : "ILP32";
}

class LibraryTest : public ::testing::TestWithParam<DataModel> {
protected:
    TestContext context{GetParam()};

    [[nodiscard]] bool lp64() const { return GetParam() == DataModel::LP64; }
    [[nodiscard]] Memory& memory() { return context.memory(); }

    [[nodiscard]] value::IntegerFormat format(ScalarType type) const {
        const TargetInfo& target = context.target();
        if (type == ScalarType::Pointer) {
            return {.bits = static_cast<std::uint8_t>(target.pointerBits()), .isSigned = false};
        }
        return {.bits = static_cast<std::uint8_t>(target.layout(type).size * 8), .isSigned = target.isSigned(type)};
    }

    // ---- Arguments ----

    [[nodiscard]] value::Value integer(Int128 value, ScalarType type) const {
        return value::Value::integer(static_cast<UInt128>(value), format(type));
    }
    [[nodiscard]] value::Value intValue(Int128 value) const { return integer(value, ScalarType::Int); }
    [[nodiscard]] value::Value size(Int128 value) const { return integer(value, context.target().sizeType()); }
    [[nodiscard]] value::Value pointer(Address address) const { return integer(address, ScalarType::Pointer); }

    /// A null-terminated string in an object of its own, in `region` (Global or ReadOnly).
    Address string(std::string_view text, Region region = Region::Global) {
        const Address address = memory().allocate(region, text.size() + 1, 1, "string").value();
        std::vector<std::byte> bytes(text.size() + 1);
        for (std::size_t i = 0; i < text.size(); ++i) {
            bytes[i] = static_cast<std::byte>(text[i]);
        }
        if (region == Region::ReadOnly) {
            memory().initialize(address, bytes);
        } else {
            EXPECT_TRUE(memory().write(address, bytes).has_value());
        }
        return address;
    }

    /// An object of `size` bytes with the given contents and no terminating null character.
    Address bytesObject(std::string_view contents, Region region = Region::Global) {
        const Address address = memory().allocate(region, contents.size(), 1, "bytes").value();
        std::vector<std::byte> bytes(contents.size());
        for (std::size_t i = 0; i < contents.size(); ++i) {
            bytes[i] = static_cast<std::byte>(contents[i]);
        }
        EXPECT_TRUE(memory().write(address, bytes).has_value());
        return address;
    }

    /// An uninitialized object, like a local array.
    Address buffer(std::uint64_t size, Region region = Region::Stack) {
        return memory().allocate(region, size, 16, "buffer").value();
    }

    // ---- Calls ----

    LibraryResult call(std::string_view name, const std::vector<value::Value>& arguments) {
        const LibraryFunction function = findLibraryFunction(name);
        if (function == nullptr) {
            ADD_FAILURE() << "the library has no function " << name;
            return {};
        }
        return function(context, LibraryCall{.name = name, .arguments = arguments});
    }

    /// The integer a call returned (sign-extended if its type is signed); fails the test if it did not return one.
    Int128 returned(const LibraryResult& result) {
        EXPECT_EQ(result.kind, LibraryResult::Kind::Return) << result.message;
        if (result.kind != LibraryResult::Kind::Return || result.value.kind() != value::Value::Kind::Integer) {
            ADD_FAILURE() << "no integer was returned";
            return 0;
        }
        return result.value.signedBits();
    }

    Int128 callInteger(std::string_view name, const std::vector<value::Value>& arguments) {
        return returned(call(name, arguments));
    }

    Address callPointer(std::string_view name, const std::vector<value::Value>& arguments) {
        return static_cast<Address>(returned(call(name, arguments)));
    }

    // ---- Memory ----

    std::vector<std::byte> readBytes(Address address, std::size_t size) {
        std::vector<std::byte> bytes(size);
        EXPECT_TRUE(memory().read(address, bytes).has_value());
        return bytes;
    }

    std::string readText(Address address, std::size_t size) {
        std::string text;
        for (const std::byte byte : readBytes(address, size)) {
            text.push_back(static_cast<char>(byte));
        }
        return text;
    }

    /// The null-terminated string at `address`.
    std::string readString(Address address) {
        std::string text;
        for (std::array<std::byte, 1> byte{}; memory().read(address, byte).has_value() && byte[0] != std::byte{0};
             ++address) {
            text.push_back(static_cast<char>(byte[0]));
        }
        return text;
    }

    Int128 readInteger(Address address, ScalarType type) {
        const value::IntegerFormat integerFormat = format(type);
        return value::decodeInteger(readBytes(address, integerFormat.bits / 8u), integerFormat).signedBits();
    }

    void writeInteger(Address address, Int128 value, ScalarType type) {
        const value::IntegerFormat integerFormat = format(type);
        std::vector<std::byte> bytes(integerFormat.bits / 8u);
        value::encodeInteger(integer(value, type), integerFormat, bytes);
        EXPECT_TRUE(memory().write(address, bytes).has_value());
    }

    /// The program's errno.
    Int128 errnoValue() { return readInteger(context.objectAddress("errno"), ScalarType::Int); }
};

/// Whether a call ended with `kind` and a message that contains `part`.
inline ::testing::AssertionResult endedWith(const LibraryResult& result, LibraryResult::Kind kind,
                                            std::string_view part = {}) {
    if (result.kind != kind) {
        return ::testing::AssertionFailure()
               << "the call ended with kind " << static_cast<int>(result.kind) << " (" << result.message << ")";
    }
    if (!result.message.contains(part)) {
        return ::testing::AssertionFailure() << "the message \"" << result.message << "\" lacks \"" << part << "\"";
    }
    return ::testing::AssertionSuccess();
}

inline ::testing::AssertionResult isUndefined(const LibraryResult& result, std::string_view part = {}) {
    return endedWith(result, LibraryResult::Kind::UndefinedBehavior, part);
}

} // namespace nitwit::interp::test
