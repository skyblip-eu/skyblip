// core/util/varint.h: ADS-L "variable-range" field codecs: UnsVR/SignVR range
#ifndef SKYBLIP_CORE_UTIL_VARINT_H
#define SKYBLIP_CORE_UTIL_VARINT_H

#include <cstdint>

namespace skyblip {

template <class T, int Bits>
T uns_vr_decode(T value) {
    const T thres = static_cast<T>(1) << Bits;
    uint8_t range = value >> Bits;
    value &= thres - 1;
    if (range == 0) return value;
    if (range == 1) return thres + 1 + (value << 1);
    if (range == 2) return 3 * thres + 2 + (value << 2);
    return 7 * thres + 4 + (value << 3);
}

template <class T, int Bits>
T uns_vr_encode(T value) {
    const T thres = static_cast<T>(1) << Bits;
    if (value < thres) return value;
    if (value < 3 * thres) return thres | ((value - thres) >> 1);
    if (value < 7 * thres) return 2 * thres | ((value - 3 * thres) >> 2);
    if (value < 15 * thres) return 3 * thres | ((value - 7 * thres) >> 3);
    return 4 * thres - 1;
}

template <class T, int Bits>
T sign_vr_encode(T value) {
    const T sign_mask = static_cast<T>(1) << (Bits + 2);
    T sign = 0;
    if (value < 0) {
        value = -value;
        sign = sign_mask;
    }
    value = uns_vr_encode<T, Bits>(value);
    return value | sign;
}

template <class T, int Bits>
T sign_vr_decode(T value) {
    const T sign_mask = static_cast<T>(1) << (Bits + 2);
    T sign = value & sign_mask;
    value = uns_vr_decode<T, Bits>(value & (sign_mask - 1));
    return sign ? -value : value;
}

inline uint16_t encode_ur2v8(uint16_t v) {
    if (v < 0x100) return v;
    if (v < 0x300) return 0x100 | ((v - 0x100) >> 1);
    if (v < 0x700) return 0x200 | ((v - 0x300) >> 2);
    if (v < 0xF00) return 0x300 | ((v - 0x700) >> 3);
    return 0x3FF;
}
inline uint16_t decode_ur2v8(uint16_t v) {
    uint16_t range = v >> 8;
    v &= 0x0FF;
    if (range == 0) return v;
    if (range == 1) return 0x101 + (v << 1);
    if (range == 2) return 0x302 + (v << 2);
    return 0x704 + (v << 3);
}

}

#endif
