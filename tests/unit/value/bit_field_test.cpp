#include <array>
#include <cstddef>

#include <gtest/gtest.h>

#include "value/arithmetic.hpp"
#include "value_formats.hpp"

namespace nitwit::value {
namespace {

using namespace test;

// The storage of
//     struct S { unsigned a : 3; int b : 9; unsigned c : 1; int d : 1; unsigned long long e : 40; int f : 32; };
// after memset(&s, 0xAA, sizeof s) and a = 5, b = -200, c = 1, d = -1, e = 0x9876543210, f = -123456789, as gcc-14
// -m64 and gcc-13 -m32 lay it out (the first 12 bytes; -m64 adds 4 bytes of tail padding). Fields are allocated from
// the least significant bit: a at bit 0, b at 3 (across a byte boundary), c at 12, d at 13, e at 14, f at 64.
constexpr std::array<unsigned, 12> structImage{0xc5, 0x39, 0x84, 0x0c, 0x95, 0x1d, 0xa6, 0xaa, 0xeb, 0x32, 0xa4, 0xf8};

struct Field {
    unsigned offset;
    unsigned width;
    IntegerFormat format;
    Int128 value;
};

constexpr std::array<Field, 6> fields{{
    {.offset = 0, .width = 3, .format = u32, .value = 5},
    {.offset = 3, .width = 9, .format = s32, .value = -200},
    {.offset = 12, .width = 1, .format = u32, .value = 1},
    {.offset = 13, .width = 1, .format = s32, .value = -1},
    {.offset = 14, .width = 40, .format = u64, .value = 0x98'7654'3210},
    {.offset = 64, .width = 32, .format = s32, .value = -123456789},
}};

std::array<std::byte, 12> filled(unsigned byte) {
    std::array<std::byte, 12> storage{};
    storage.fill(static_cast<std::byte>(byte));
    return storage;
}

std::array<std::byte, 12> image() {
    std::array<std::byte, 12> storage{};
    for (std::size_t i = 0; i < storage.size(); ++i) {
        storage[i] = static_cast<std::byte>(structImage[i]);
    }
    return storage;
}

TEST(BitFieldTest, ReadsTheFieldsGccWrote) {
    const auto storage = image();
    for (const Field& field : fields) {
        SCOPED_TRACE(field.offset);
        EXPECT_EQ(readBitField(storage, field.offset, field.width, field.format).bits(),
                  integer(field.value, field.format).bits());
    }
}

TEST(BitFieldTest, WritesTheBytesGccWrites) {
    auto storage = filled(0xAA);
    for (const Field& field : fields) {
        writeBitField(storage, field.offset, field.width, integer(field.value, field.format));
    }
    EXPECT_EQ(storage, image());
}

TEST(BitFieldTest, AllOnesReadAccordingToSignedness) {
    // gcc: a = 7, b = -1, c = 1, d = -1, e = 0xffffffffff, f = -1.
    const auto storage = filled(0xFF);
    EXPECT_EQ(readBitField(storage, 0, 3, u32).bits(), 7u);
    EXPECT_EQ(readBitField(storage, 3, 9, s32).signedBits(), -1);
    EXPECT_EQ(readBitField(storage, 12, 1, u32).bits(), 1u);
    EXPECT_EQ(readBitField(storage, 13, 1, s32).signedBits(), -1);
    EXPECT_EQ(readBitField(storage, 14, 40, u64).bits(), 0xFF'FFFF'FFFFu);
    EXPECT_EQ(readBitField(storage, 64, 32, s32).signedBits(), -1);
}

TEST(BitFieldTest, WriteKeepsTheLowBitsAndTheNeighbours) {
    auto storage = filled(0x00);
    // Assigning 0x1FF to a 3-bit field stores 7 (C17 6.3.1.3p2 for the unsigned field).
    writeBitField(storage, 5, 3, Value::integer(0x1FF, u32));
    EXPECT_EQ(storage[0], std::byte{0xE0});
    EXPECT_EQ(storage[1], std::byte{0x00});
    EXPECT_EQ(readBitField(storage, 5, 3, u32).bits(), 7u);
    EXPECT_EQ(readBitField(storage, 5, 3, s32).signedBits(), -1);
}

TEST(BitFieldTest, FullWidthFieldAtAnUnalignedOffset) {
    auto storage = filled(0x00);
    writeBitField(storage, 5, 64, Value::integer(0x8123'4567'89AB'CDEF, u64));
    EXPECT_EQ(readBitField(storage, 5, 64, u64).bits(), 0x8123'4567'89AB'CDEFu);
    EXPECT_EQ(readBitField(storage, 5, 64, s64).bits(), Value::integer(0x8123'4567'89AB'CDEF, s64).bits());
    EXPECT_EQ(storage[0], std::byte{0xE0}); // 0xEF << 5, low byte
    EXPECT_EQ(storage[8], std::byte{0x10}); // 0x81 >> 3
    EXPECT_EQ(storage[9], std::byte{0x00});
}

TEST(BitFieldTest, OneBitBoolField) {
    auto storage = filled(0x00);
    writeBitField(storage, 7, 1, Value::integer(1, u8));
    EXPECT_EQ(storage[0], std::byte{0x80});
    EXPECT_EQ(readBitField(storage, 7, 1, u8).bits(), 1u);
}

TEST(BitFieldTest, Int128FieldAcrossSeventeenBytes) {
    std::array<std::byte, 17> storage{};
    const UInt128 value = ~UInt128{0} - 0x1234;
    writeBitField(storage, 3, 128, Value::integer(value, u128));
    EXPECT_EQ(readBitField(storage, 3, 128, u128).bits(), value);
    EXPECT_EQ(readBitField(storage, 3, 128, s128).signedBits(), -0x1235);
    EXPECT_EQ(storage[16], std::byte{0x07});
}

TEST(BitFieldDeathTest, FieldOutsideTheStorageAsserts) {
    const auto storage = filled(0x00);
    EXPECT_DEATH((void)readBitField(storage, 90, 7, u32), "outside its storage");
    // bitOffset + width wraps around in unsigned arithmetic.
    EXPECT_DEATH((void)readBitField(storage, ~0u - 2, 7, u32), "outside its storage");
    EXPECT_DEATH((void)readBitField(storage, 0, 33, u32), "wider than its type");
}

} // namespace
} // namespace nitwit::value
