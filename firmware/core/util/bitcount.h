// core/util/bitcount.h: popcount over a byte, a word or a buffer, for counting
// the bits a syndrome disagrees on. Compiler builtins, so a target with a
// popcount instruction uses it.
#ifndef SKYBLIP_CORE_UTIL_BITCOUNT_H
#define SKYBLIP_CORE_UTIL_BITCOUNT_H

#include <cstddef>
#include <cstdint>

namespace skyblip {

inline uint8_t count_ones(uint8_t b) { return static_cast<uint8_t>(__builtin_popcount(b)); }
inline uint8_t count_ones(uint16_t w) { return static_cast<uint8_t>(__builtin_popcount(w)); }
inline uint8_t count_ones(uint32_t w) { return static_cast<uint8_t>(__builtin_popcountl(w)); }
inline uint8_t count_ones(uint64_t w) { return static_cast<uint8_t>(__builtin_popcountll(w)); }

inline uint32_t count_ones(const uint8_t* bytes, size_t n) {
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += count_ones(bytes[i]);
    return sum;
}

}

#endif
