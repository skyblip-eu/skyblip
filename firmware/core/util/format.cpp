#include "core/util/format.h"

namespace skyblip {

int fmt_string(char* out, const char* s) {
    int n = 0;
    while (s && s[n]) {
        out[n] = s[n];
        n++;
    }
    return n;
}

int fmt_hex(char* out, uint32_t value, uint8_t digits) {
    for (int i = digits - 1; i >= 0; i--) {
        out[i] = hex_digit(static_cast<uint8_t>(value & 0x0F));
        value >>= 4;
    }
    return digits;
}

int fmt_uint(char* out, uint32_t value, uint8_t min_digits, uint8_t dec_point) {
    char tmp[12];
    int len = 0;
    do {
        tmp[len++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value);
    while (len < min_digits) tmp[len++] = '0';
    if (dec_point) {
        while (len <= dec_point) tmp[len++] = '0';
    }
    int n = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (dec_point && i + 1 == dec_point) out[n++] = '.';
        out[n++] = tmp[i];
    }
    return n;
}

int fmt_int(char* out, int32_t value, uint8_t min_digits, uint8_t dec_point, bool no_plus) {
    int n = 0;
    if (value < 0) {
        out[n++] = '-';
        value = -value;
    } else if (!no_plus) {
        out[n++] = '+';
    }
    n += fmt_uint(out + n, static_cast<uint32_t>(value), min_digits, dec_point);
    return n;
}

namespace {
int fmt_dm(char* out, int32_t coord_1e7, int deg_digits) {
    uint32_t a = coord_1e7 < 0 ? -coord_1e7 : coord_1e7;
    uint32_t deg = a / 10000000u;
    uint64_t rem = static_cast<uint64_t>(a) - static_cast<uint64_t>(deg) * 10000000u;
    uint64_t min_e4 = rem * 60u * 10000u / 10000000u;
    uint32_t mm = static_cast<uint32_t>(min_e4 / 10000u);
    uint32_t frac = static_cast<uint32_t>(min_e4 % 10000u);
    int n = 0;
    n += fmt_uint(out + n, deg, static_cast<uint8_t>(deg_digits));
    n += fmt_uint(out + n, mm, 2);
    out[n++] = '.';
    n += fmt_uint(out + n, frac, 4);
    return n;
}
}

int fmt_nmea_lat(char* out, int32_t lat_1e7) {
    int n = fmt_dm(out, lat_1e7, 2);
    out[n++] = ',';
    out[n++] = lat_1e7 < 0 ? 'S' : 'N';
    return n;
}

int fmt_nmea_lon(char* out, int32_t lon_1e7) {
    int n = fmt_dm(out, lon_1e7, 3);
    out[n++] = ',';
    out[n++] = lon_1e7 < 0 ? 'W' : 'E';
    return n;
}

}
