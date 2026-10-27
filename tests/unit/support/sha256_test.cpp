#include "support/sha256.hpp"

#include <string>

#include <gtest/gtest.h>

namespace nitwit {
namespace {

// The examples of NIST (FIPS 180-4, "SHA256.pdf" and "SHA2_Additional.pdf").
TEST(Sha256Test, TheEmptyMessage) {
    EXPECT_EQ(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256Test, OneBlock) {
    EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256Test, TwoBlocksWhenTheLengthNoLongerFitsTheFirst) {
    EXPECT_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256Test, ExactlyOneBlockOfData) {
    EXPECT_EQ(sha256Hex(std::string(64, 'a')), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST(Sha256Test, AMillionBytes) {
    EXPECT_EQ(sha256Hex(std::string(1'000'000, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256Test, BytesAboveSevenBitsAndZeros) {
    // printf '\x00\xff\x80' | sha256sum
    EXPECT_EQ(sha256Hex(std::string("\x00\xff\x80", 3)),
              "f742b965f156c10374bc23aea96e3a8aff8facd6fc079defeaa30219ad86f211");
}

} // namespace
} // namespace nitwit
