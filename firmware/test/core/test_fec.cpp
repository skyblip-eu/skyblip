#include <cstdint>
#include <cstring>

#include "core/fec/crc.h"
#include "core/fec/manchester.h"
#include "core/fec/scramble.h"
#include "doctest/doctest.h"

using namespace skyblip::fec;

TEST_CASE("crc: adsl PI detects a good vs corrupted packet") {
    uint8_t data[24];
    for (int i = 0; i < 21; i++) data[i] = static_cast<uint8_t>(i * 7 + 1);
    uint32_t pi = adsl_pi_calc(data, 21);
    data[21] = static_cast<uint8_t>(pi >> 16);
    data[22] = static_cast<uint8_t>(pi >> 8);
    data[23] = static_cast<uint8_t>(pi);
    CHECK(adsl_pi_check(data, 24) == 0);  // consistent
    data[5] ^= 0x10;                      // corrupt one bit
    CHECK(adsl_pi_check(data, 24) != 0);
}

TEST_CASE("crc: crc16-ccitt and crc32 known/stability") {
    const uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc16_ccitt(msg, 9) == 0x31C3);  // CCITT-FALSE(init0) check value
    uint32_t a = crc32(msg, 9);
    uint32_t b = crc32(msg, 9);
    CHECK(a == b);
    uint8_t msg2[9];
    std::memcpy(msg2, msg, 9);
    msg2[0] ^= 1;
    CHECK(crc32(msg2, 9) != a);
}

TEST_CASE("manchester: encode/decode round-trip is clean") {
    uint8_t data[21];
    for (int i = 0; i < 21; i++) data[i] = static_cast<uint8_t>(i * 11 + 3);
    uint8_t coded[42];
    manchester_encode(data, 21, coded);
    uint8_t out[21], err[21];
    size_t bad = manchester_decode(coded, 21, out, err);
    CHECK(bad == 0);
    CHECK(std::memcmp(data, out, 21) == 0);
    for (int i = 0; i < 21; i++) CHECK(err[i] == 0);
}

TEST_CASE("manchester: a flipped chip is flagged as an error bit") {
    uint8_t data[4] = {0xA5, 0x00, 0xFF, 0x3C};
    uint8_t coded[8];
    manchester_encode(data, 4, coded);
    coded[0] ^= 0x80;  // break one Manchester symbol -> illegal 00/11
    uint8_t out[4], err[4];
    size_t bad = manchester_decode(coded, 4, out, err);
    CHECK(bad >= 1);
    CHECK(err[0] != 0);
}

TEST_CASE("scramble: descramble is the exact inverse (property)") {
    for (uint32_t seed = 1; seed < 5000; seed += 137) {
        uint32_t w[5];
        uint32_t s = seed;
        for (int i = 0; i < 5; i++) {
            s = s * 1664525u + 1013904223u;
            w[i] = s;
        }
        uint32_t orig[5];
        std::memcpy(orig, w, sizeof(w));
        xxtea_scramble_key0(w, 5, 6);
        CHECK(std::memcmp(w, orig, sizeof(w)) != 0);  // actually changed
        xxtea_descramble_key0(w, 5, 6);
        CHECK(std::memcmp(w, orig, sizeof(w)) == 0);  // and reversible
    }
}

TEST_CASE("scramble: avalanche — one input bit flips ~half the output bits") {
    uint32_t w[5] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555};
    uint32_t a[5];
    std::memcpy(a, w, sizeof(w));
    xxtea_scramble_key0(a, 5, 6);
    w[2] ^= 1;  // flip a single bit
    uint32_t b[5];
    std::memcpy(b, w, sizeof(w));
    xxtea_scramble_key0(b, 5, 6);
    int diff = 0;
    for (int i = 0; i < 5; i++) diff += __builtin_popcountl(a[i] ^ b[i]);
    // 160 output bits; a good diffusion is roughly half.
    CHECK(diff > 50);
    CHECK(diff < 110);
}
