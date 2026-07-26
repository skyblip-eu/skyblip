#include "core/fec/scramble.h"

namespace skyblip::fec {

namespace {
inline uint32_t mx_key0(uint32_t y, uint32_t z, uint32_t sum) {
    return (((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ ((sum ^ y) + z);
}
constexpr uint32_t kDelta = 0x9e3779b9u;
}

void xxtea_scramble_key0(uint32_t* data, uint8_t words, uint8_t loops) {
    uint32_t sum = 0;
    uint32_t z = data[words - 1];
    uint32_t y;
    for (; loops; loops--) {
        sum += kDelta;
        for (uint8_t p = 0; p < static_cast<uint8_t>(words - 1); p++) {
            y = data[p + 1];
            z = data[p] += mx_key0(y, z, sum);
        }
        y = data[0];
        z = data[words - 1] += mx_key0(y, z, sum);
    }
}

void xxtea_descramble_key0(uint32_t* data, uint8_t words, uint8_t loops) {
    uint32_t sum = loops * kDelta;
    uint32_t y = data[0];
    uint32_t z;
    for (; loops; loops--) {
        for (uint8_t p = static_cast<uint8_t>(words - 1); p; p--) {
            z = data[p - 1];
            y = data[p] -= mx_key0(y, z, sum);
        }
        z = data[words - 1];
        y = data[0] -= mx_key0(y, z, sum);
        sum -= kDelta;
    }
}

}
