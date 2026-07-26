#include "core/gnss/nmea.h"

#include <cstring>

namespace skyblip::gnss {

namespace {
int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
long parse_long(const char* s, int len) {
    long v = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') break;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}
int d2(const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); }

uint32_t to_epoch(int y, int mon, int day, int hh, int mm, int ss) {
    y -= mon <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097L + static_cast<long>(doe) - 719468L;
    return static_cast<uint32_t>(days * 86400L + hh * 3600L + mm * 60L + ss);
}
}

bool nmea_checksum_ok(const char* line, int len) {
    if (len < 4 || line[0] != '$') return false;
    int star = -1;
    for (int i = 0; i < len; i++)
        if (line[i] == '*') {
            star = i;
            break;
        }
    if (star < 0 || star + 2 >= len) return false;
    uint8_t cs = 0;
    for (int i = 1; i < star; i++) cs ^= static_cast<uint8_t>(line[i]);
    int hi = hexval(line[star + 1]);
    int lo = hexval(line[star + 2]);
    if (hi < 0 || lo < 0) return false;
    return cs == (hi << 4 | lo);
}

int32_t nmea_parse_coord(const char* dm, char hemi) {
    int dot = -1;
    for (int i = 0; dm[i] && i < 16; i++)
        if (dm[i] == '.') {
            dot = i;
            break;
        }
    if (dot < 2) return 0;
    int deg_digits = dot - 2;
    long deg = parse_long(dm, deg_digits);
    long min_whole = parse_long(dm + deg_digits, 2);
    long frac = 0, scale = 1;
    for (int i = 0; i < 4; i++) {
        char c = dm[dot + 1 + i];
        if (c < '0' || c > '9') break;
        frac = frac * 10 + (c - '0');
        scale *= 10;
    }
    long min_e4 = min_whole * 10000 + (scale ? frac * (10000 / scale) : 0);
    int64_t v = static_cast<int64_t>(deg) * 10000000LL +
                static_cast<int64_t>(min_e4) * 10000000LL / 600000LL;
    if (hemi == 'S' || hemi == 'W') v = -v;
    return static_cast<int32_t>(v);
}

bool NmeaParser::feed(char c) {
    if (c == '$') {
        pos_ = 0;
        buf_[pos_++] = c;
        return false;
    }
    if (c == '\r' || c == '\n') {
        if (pos_ > 0) {
            bool ok = parse_line(buf_, pos_);
            pos_ = 0;
            return ok;
        }
        return false;
    }
    if (pos_ > 0 && pos_ < static_cast<int>(sizeof(buf_)) - 1) buf_[pos_++] = c;
    return false;
}

bool NmeaParser::parse_line(const char* line, int len) {
    if (!nmea_checksum_ok(line, len)) return false;
    char tmp[100];
    int n = 0;
    for (int i = 0; i < len && line[i] != '*' && n < 99; i++) tmp[n++] = line[i];
    tmp[n] = 0;
    const char* fields[24];
    int nf = 0;
    fields[nf++] = tmp;
    for (int i = 0; i < n && nf < 24; i++) {
        if (tmp[i] == ',') {
            tmp[i] = 0;
            fields[nf++] = tmp + i + 1;
        }
    }
    if (nf < 1) return false;
    const char* tag = fields[0];
    if (strlen(tag) >= 6 && memcmp(tag + 3, "RMC", 3) == 0) return apply_rmc(fields, nf);
    if (strlen(tag) >= 6 && memcmp(tag + 3, "GGA", 3) == 0) return apply_gga(fields, nf);
    return false;
}

bool NmeaParser::apply_rmc(const char* f[], int nf) {
    if (nf < 10) return false;
    bool valid = f[2][0] == 'A';
    fix_.valid = valid;
    if (f[1][0] && f[9][0] && strlen(f[1]) >= 6 && strlen(f[9]) >= 6) {
        int hh = d2(f[1]), mm = d2(f[1] + 2), ss = d2(f[1] + 4);
        int day = d2(f[9]), mon = d2(f[9] + 2), yy = d2(f[9] + 4);
        int year = yy >= 80 ? 1900 + yy : 2000 + yy;
        fix_.utc = to_epoch(year, mon, day, hh, mm, ss);
        fix_.utc_valid = true;
    }
    if (valid) {
        if (f[3][0]) fix_.lat_1e7 = nmea_parse_coord(f[3], f[4][0]);
        if (f[5][0]) fix_.lon_1e7 = nmea_parse_coord(f[5], f[6][0]);
        if (f[7][0]) {
            long kn_e1 = 0, sc = 1;
            const char* s = f[7];
            long ip = 0;
            while (*s >= '0' && *s <= '9') ip = ip * 10 + (*s++ - '0');
            long fp = 0;
            if (*s == '.') {
                s++;
                if (*s >= '0' && *s <= '9') fp = (*s - '0');
            }
            kn_e1 = ip * 10 + fp;
            (void)sc;
            fix_.speed_q = static_cast<uint16_t>((kn_e1 * 2058 + 5000) / 10000);
        }
        if (f[8][0]) {
            long ip = 0;
            const char* s = f[8];
            while (*s >= '0' && *s <= '9') ip = ip * 10 + (*s++ - '0');
            fix_.track_c9 = static_cast<uint16_t>((ip * 512 + 180) / 360 % 512);
        }
    }
    fix_.updates++;
    return true;
}

bool NmeaParser::apply_gga(const char* f[], int nf) {
    if (nf < 10) return false;
    fix_.fix_quality = static_cast<uint8_t>(parse_long(f[6], 2));
    fix_.sats = static_cast<uint8_t>(parse_long(f[7], 2));
    if (f[9][0]) {
        long ip = 0;
        const char* s = f[9];
        bool neg = false;
        if (*s == '-') {
            neg = true;
            s++;
        }
        while (*s >= '0' && *s <= '9') ip = ip * 10 + (*s++ - '0');
        fix_.alt_m = static_cast<int32_t>(neg ? -ip : ip);
    }
    fix_.updates++;
    return true;
}

}
