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

// Rounded, not truncated: "46.9" is 47 m of geoid separation, not 46.
bool parse_scaled(const char* s, long scale, long& out) {
    if (!s || !s[0]) return false;
    bool neg = false;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    if (*s < '0' || *s > '9') return false;
    long whole = 0;
    while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0');
    long value = whole * scale * 10;
    if (*s == '.') {
        s++;
        long place = scale * 10;
        while (*s >= '0' && *s <= '9' && place > 0) {
            place /= 10;
            value += (*s++ - '0') * place;
        }
    }
    value = (value + 5) / 10;
    out = neg ? -value : value;
    return true;
}

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
    if (strlen(tag) < 6) return false;
    if (memcmp(tag + 3, "RMC", 3) == 0) return apply_rmc(fields, nf);
    if (memcmp(tag + 3, "GGA", 3) == 0) return apply_gga(fields, nf, len);
    if (memcmp(tag + 3, "TXT", 3) == 0) return apply_txt(line, len);
    return false;
}

// $GPTXT,01,01,02,SW=URANUS5,V5.1.0.0 - the CASIC firmware banner, and the only
// evidence that the part answering us speaks $PCAS at all. The version text is
// everything after "SW=", which is where SoftRF reads it from too.
// The version text carries commas of its own ("SW=URANUS5,V5.1.0.0"), so it is
// read off the raw line rather than out of the split fields: it is one string,
// not three, and SoftRF takes the same run of characters up to the checksum.
bool NmeaParser::apply_txt(const char* line, int len) {
    int at = 0;
    for (int commas = 0; at < len && commas < 4; at++)
        if (line[at] == ',') commas++;
    // Only the SW= banner. The part also emits $GPTXT for antenna status and
    // start-up notices, and a version field that is sometimes an antenna warning
    // is worse than no version field.
    if (len - at < 3 || line[at] != 'S' || line[at + 1] != 'W' || line[at + 2] != '=') return false;
    at += 3;
    int n = 0;
    for (; at < len && line[at] != '*' && n < kVersionCap - 1; at++, n++) version_[n] = line[at];
    version_[n] = 0;
    last_ = Sentence::Txt;
    // Not a solution: `updates` counts fixes, and the verification window that
    // reads it must not be satisfied by the receiver introducing itself.
    return n > 0;
}

// INFO: fc 03aug26 RMC fields 1 and 9 are UTC as the receiver already resolved
// it, so the GPS-UTC leap second count never enters this arithmetic. The
// moshe-braner fork queries and persists the count and reboots when it changes
// (.../src/driver/GNSS.cpp:1610-1679) because it reads u-blox NAV-TIMEGPS, which
// reports GPS time; that correction has no reader here and adding one would be a
// second, wrong, clock. This device's only failure mode in that direction is a
// receiver whose almanac is stale, and the date sanity below is what catches it.
bool NmeaParser::apply_rmc(const char* f[], int nf) {
    if (nf < 10) return false;
    last_ = Sentence::Rmc;
    bool valid = f[2][0] == 'A';
    fix_.valid = valid;
    fix_.utc_valid = false;
    if (f[1][0] && f[9][0] && strlen(f[1]) >= 6 && strlen(f[9]) >= 6) {
        int hh = d2(f[1]), mm = d2(f[1] + 2), ss = d2(f[1] + 4);
        int day = d2(f[9]), mon = d2(f[9] + 2), yy = d2(f[9] + 4);
        // The MTK 1980 lie and its neighbours: a two-digit year of 70 or more is
        // a receiver that has not decoded the almanac, not a date.
        const bool date_sane = yy < kMaxTwoDigitYear && mon >= 1 && mon <= 12 && day >= 1 &&
                               day <= 31 && hh < 24 && mm < 60 && ss < 62;
        if (date_sane) {
            fix_.utc = to_epoch(2000 + yy, mon, day, hh, mm, ss);
            fix_.utc_valid = true;
        }
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

bool NmeaParser::apply_gga(const char* f[], int nf, int len) {
    // A sentence that stopped early still checksums: length is the only thing
    // that catches it, which is why moshe-braner measures it.
    if (nf < 10 || len < kMinGgaLength) return false;
    last_ = Sentence::Gga;
    fix_.fix_quality = static_cast<uint8_t>(parse_long(f[6], 2));
    fix_.sats = static_cast<uint8_t>(parse_long(f[7], 2));

    long hdop_e2 = 0;
    fix_.hdop_e2 = parse_scaled(f[8], 100, hdop_e2) && hdop_e2 > 0 && hdop_e2 <= 0xFFFF
                       ? static_cast<uint16_t>(hdop_e2)
                       : 0;

    long separation_m = 0;
    fix_.geoid_separation_measured = nf > 11 && parse_scaled(f[11], 1, separation_m) &&
                                     separation_m != 0 && separation_m > -200 && separation_m < 200;
    fix_.geoid_separation_m = fix_.geoid_separation_measured ? static_cast<int32_t>(separation_m)
                                                             : kDefaultGeoidSeparationM;

    long msl_m = 0;
    fix_.alt_msl_valid = parse_scaled(f[9], 1, msl_m);
    fix_.alt_hae_valid = fix_.alt_msl_valid;
    if (fix_.alt_msl_valid) {
        fix_.alt_msl_m = static_cast<int32_t>(msl_m);
        fix_.alt_m = fix_.alt_msl_m + fix_.geoid_separation_m;
    }

    fix_.updates++;
    return true;
}

}
