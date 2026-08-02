// core/util/format.h: tiny, allocation-free number/string formatting into a
#ifndef SKYBLIP_CORE_UTIL_FORMAT_H
#define SKYBLIP_CORE_UTIL_FORMAT_H

#include <cstdint>

namespace skyblip {

inline char hex_digit(uint8_t v) {
    v &= 0x0F;
    return static_cast<char>(v < 10 ? '0' + v : 'A' + v - 10);
}

int fmt_string(char* out, const char* s);

int fmt_hex(char* out, uint32_t value, uint8_t digits);

int fmt_uint(char* out, uint32_t value, uint8_t min_digits = 1, uint8_t dec_point = 0);

int fmt_int(char* out, int32_t value, uint8_t min_digits = 1, uint8_t dec_point = 0,
            bool no_plus = false);

int fmt_nmea_lat(char* out, int32_t lat_1e7);
int fmt_nmea_lon(char* out, int32_t lon_1e7);

}

#endif
