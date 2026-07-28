#include "core/protocol/nmea_out.h"

#include "core/util/format.h"
#include "core/util/intmath.h"
#include "core/util/units.h"

namespace skyblip::protocol {

int nmea_finish(char* s, int body_len) {
    uint8_t cs = 0;
    for (int i = 1; i < body_len; i++) cs ^= static_cast<uint8_t>(s[i]);
    int n = body_len;
    s[n++] = '*';
    s[n++] = hex_digit(static_cast<uint8_t>(cs >> 4));
    s[n++] = hex_digit(static_cast<uint8_t>(cs & 0x0F));
    s[n++] = '\r';
    s[n++] = '\n';
    s[n] = 0;
    return n;
}

uint8_t adsl_cat_to_alptas(uint8_t c) {
    static const uint8_t kMap[18] = {0x0, 0x8, 0x8, 0x3, 0x1, 0xB, 0xC, 0x7, 0x4,
                                     0x8, 0x3, 0xD, 0xD, 0xD, 0x0, 0x0, 0x0, 0x0};
    return c < 18 ? kMap[c] : 0;
}

uint8_t addr_table_to_idtype(uint8_t t) {
    if (t == 0x05) return 1;
    if (t == 0x06) return 2;
    return 0;
}

bool relative_ned(const messages::OwnState& own, const messages::AircraftObs& t, int32_t& north_m,
                  int32_t& east_m, int32_t& up_m) {
    if (!own.fix_valid || !t.valid_pos) return false;
    int64_t dlat = static_cast<int64_t>(t.lat_1e7) - own.lat_1e7;
    int64_t dlon = static_cast<int64_t>(t.lon_1e7) - own.lon_1e7;
    north_m = static_cast<int32_t>((dlat * 11132) / 1000000);
    int16_t ang = static_cast<int16_t>((static_cast<int64_t>(own.lat_1e7) * 65536) / 3600000000LL);
    int32_t coslat = icos(ang);
    int64_t east = (dlon * 11132) / 1000000;
    east_m = static_cast<int32_t>((east * coslat) >> 14);
    up_m = t.alt_m - own.alt_m;
    return true;
}

int format_pflaa(char* out, size_t cap, const messages::OwnState& own,
                 const messages::AircraftObs& t, uint8_t alarm_level) {
    (void)cap;
    int32_t n_m, e_m, u_m;
    if (!relative_ned(own, t, n_m, e_m, u_m)) return 0;
    int n = 0;
    n += fmt_string(out + n, "$PFLAA,");
    n += fmt_uint(out + n, alarm_level);
    out[n++] = ',';
    n += fmt_int(out + n, n_m, 1, 0, true);
    out[n++] = ',';
    n += fmt_int(out + n, e_m, 1, 0, true);
    out[n++] = ',';
    n += fmt_int(out + n, u_m, 1, 0, true);
    out[n++] = ',';
    n += fmt_uint(out + n, addr_table_to_idtype(t.addr_table));
    out[n++] = ',';
    n += fmt_hex(out + n, t.addr, 6);
    out[n++] = ',';
    if (t.flight_state != 1) {
        uint16_t deg = to_degrees(Cordic9(t.track_c9)).v;
        n += fmt_uint(out + n, deg);
    }
    out[n++] = ',';
    out[n++] = ',';
    if (t.has_speed) n += fmt_uint(out + n, (static_cast<uint32_t>(t.speed_q) + 2) / 4);
    out[n++] = ',';
    if (t.has_climb) {
        int32_t climb_dm = (static_cast<int32_t>(t.climb_e8) * 10 + 4) / 8;
        n += fmt_int(out + n, climb_dm, 1, 1, true);
    } else {
        out[n++] = '0';
    }
    out[n++] = ',';
    out[n++] = hex_digit(adsl_cat_to_alptas(t.aircraft_cat));
    return nmea_finish(out, n);
}

int format_pflau(char* out, size_t cap, const messages::OwnState& own, int n_targets,
                 const messages::AircraftObs* threat, uint8_t alarm_level, uint16_t rel_bearing_deg,
                 int32_t rel_vert_m, int32_t rel_dist_m) {
    (void)cap;
    int n = 0;
    n += fmt_string(out + n, "$PFLAU,");
    n += fmt_uint(out + n, static_cast<uint32_t>(n_targets));
    out[n++] = ',';
    out[n++] = own.utc_valid ? '1' : '0';
    out[n++] = ',';
    out[n++] = static_cast<char>('0' + (own.fix_valid ? 2 : 0));
    out[n++] = ',';
    out[n++] = '1';
    out[n++] = ',';
    n += fmt_uint(out + n, alarm_level);
    out[n++] = ',';
    if (threat) {
        n += fmt_int(out + n, static_cast<int32_t>(rel_bearing_deg), 1, 0, true);
        out[n++] = ',';
        n += fmt_uint(out + n, alarm_level ? 2u : 0u);
        out[n++] = ',';
        n += fmt_int(out + n, rel_vert_m, 1, 0, true);
        out[n++] = ',';
        n += fmt_uint(out + n, static_cast<uint32_t>(rel_dist_m < 0 ? 0 : rel_dist_m));
        out[n++] = ',';
        n += fmt_hex(out + n, threat->addr, 6);
    } else {
        out[n++] = ',';
        out[n++] = '0';
        out[n++] = ',';
        out[n++] = ',';
    }
    return nmea_finish(out, n);
}

int format_pgrmz(char* out, size_t cap, int32_t alt_ft) {
    (void)cap;
    int n = 0;
    n += fmt_string(out + n, "$PGRMZ,");
    n += fmt_int(out + n, alt_ft, 1, 0, true);
    n += fmt_string(out + n, ",F,2");
    return nmea_finish(out, n);
}

}
