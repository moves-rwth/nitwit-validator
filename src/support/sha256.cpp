#include "support/sha256.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>

namespace nitwit {

namespace {

// FIPS 180-4, 4.2.2: the first 32 bits of the fractional parts of the cube roots of the first 64 primes.
constexpr std::array<std::uint32_t, 64> roundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::size_t blockBytes = 64;

using State = std::array<std::uint32_t, 8>;

// FIPS 180-4, 6.2.2: one block of 64 bytes, read as 16 big-endian words.
void compress(State& state, const unsigned char* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        words[i] = (std::uint32_t{block[4 * i]} << 24) | (std::uint32_t{block[4 * i + 1]} << 16)
                   | (std::uint32_t{block[4 * i + 2]} << 8) | std::uint32_t{block[4 * i + 3]};
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = std::rotr(words[i - 15], 7) ^ std::rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
        const std::uint32_t s1 = std::rotr(words[i - 2], 17) ^ std::rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    State v = state;
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t sum1 = std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25);
        const std::uint32_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint32_t t1 = v[7] + sum1 + choice + roundConstants[i] + words[i];
        const std::uint32_t sum0 = std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22);
        const std::uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const std::uint32_t t2 = sum0 + majority;
        v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
    }
    for (std::size_t i = 0; i < 8; ++i) {
        state[i] += v[i];
    }
}

} // namespace

std::string sha256Hex(std::string_view data) {
    // FIPS 180-4, 5.3.3: the initial hash value.
    State state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    const std::size_t whole = data.size() - data.size() % blockBytes;
    for (std::size_t at = 0; at < whole; at += blockBytes) {
        compress(state, bytes + at);
    }
    // FIPS 180-4, 5.1.1: the rest, a 1 bit, zeros, and the length in bits as a 64-bit big-endian number.
    std::array<unsigned char, 2 * blockBytes> tail{};
    const std::size_t rest = data.size() - whole;
    for (std::size_t i = 0; i < rest; ++i) {
        tail[i] = bytes[whole + i];
    }
    tail[rest] = 0x80;
    const std::size_t tailBytes = rest + 1 + 8 <= blockBytes ? blockBytes : 2 * blockBytes;
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    for (std::size_t i = 0; i < 8; ++i) {
        tail[tailBytes - 1 - i] = static_cast<unsigned char>(bits >> (8 * i));
    }
    for (std::size_t at = 0; at < tailBytes; at += blockBytes) {
        compress(state, tail.data() + at);
    }
    std::string digest;
    for (const std::uint32_t word : state) {
        digest += std::format("{:08x}", word);
    }
    return digest;
}

} // namespace nitwit
