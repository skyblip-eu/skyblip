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

// INFO: fc 03aug26 ADS-L 4 SRD860 issue 2's address table is 0-4 self-minted/
// random, 5 ICAO, 6 FLARM, 7 OGN (core/settings/address.h). $PFLAA's IDType
// carries only three values, and SkyDemon refuses the sentence for anything but
// 1 or 2 (oss/SoftRF-moshe-braner/.../libraries/OGN/ads-l.h:657-658: "if
// (AddrType==5) AddrType=1; else AddrType=2; // SkyDemon only accepts 1 or 2").
// Leaving a self-minted address at IDType 0 draws nothing on that app at all,
// which is the one failure worth never causing again. Between the two values
// left, 1 (ICAO) claims a permanent, registry-issued identity for an address we
// mint fresh every flight; 2 (FLARM) claims a device-class kinship that is at
// least true of the mechanism - transient and self-assigned - so everything
// that is not ICAO becomes FLARM. What that costs: an OGN tracker's address (7)
// and our own random one (0-4) both draw on the tablet as if they were FLARM,
// which is a lie about provenance too - just a cheaper one than claiming ICAO,
// because nothing downstream correlates a FLARM ID against an aircraft
// register the way it might an ICAO one.
uint8_t addr_table_to_idtype(uint8_t t) { return t == 0x05 ? 1 : 2; }

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
                 const messages::AircraftObs* threat, uint8_t alarm_level, int16_t rel_bearing_deg,
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

int format_pgrmz(char* out, size_t cap, int32_t alt_ft, bool fix_valid) {
    (void)cap;
    int n = 0;
    n += fmt_string(out + n, "$PGRMZ,");
    n += fmt_int(out + n, alt_ft, 1, 0, true);
    n += fmt_string(out + n, ",F,");
    out[n++] = fix_valid ? '3' : '1';
    return nmea_finish(out, n);
}

namespace {

// The inverse of the epoch core/gnss/nmea.cpp builds from a $GPRMC date and
// time: own.utc keeps no calendar fields of its own, so writing one back out
// means undoing the conversion. Howard Hinnant's civil_from_days, exact over
// the whole range a uint32_t epoch can name.
void civil_from_epoch(uint32_t utc, int& year, int& month, int& day, int& hh, int& mm, int& ss) {
    const uint32_t sod = utc % 86400u;
    hh = static_cast<int>(sod / 3600u);
    mm = static_cast<int>((sod / 60u) % 60u);
    ss = static_cast<int>(sod % 60u);
    const int64_t z = static_cast<int64_t>(utc / 86400u) + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp) + (mp < 10 ? 3 : -9);
    year = static_cast<int>(y + (month <= 2 ? 1 : 0));
}

int put_hhmmss(char* out, uint32_t utc) {
    const uint32_t sod = utc % 86400u;
    int n = 0;
    n += fmt_uint(out + n, sod / 3600u, 2);
    n += fmt_uint(out + n, (sod / 60u) % 60u, 2);
    n += fmt_uint(out + n, sod % 60u, 2);
    return n;
}

}  // namespace

int format_gprmc(char* out, size_t cap, const messages::OwnState& own) {
    (void)cap;
    if (!own.fix_valid || !own.utc_valid) return 0;
    int year, month, day, hh, mm, ss;
    civil_from_epoch(own.utc, year, month, day, hh, mm, ss);
    int n = 0;
    n += fmt_string(out + n, "$GPRMC,");
    n += fmt_uint(out + n, static_cast<uint32_t>(hh), 2);
    n += fmt_uint(out + n, static_cast<uint32_t>(mm), 2);
    n += fmt_uint(out + n, static_cast<uint32_t>(ss), 2);
    n += fmt_string(out + n, ",A,");
    n += fmt_nmea_lat(out + n, own.lat_1e7);
    out[n++] = ',';
    n += fmt_nmea_lon(out + n, own.lon_1e7);
    out[n++] = ',';
    // Ground speed is knots on this sentence, and own.speed_q is quarter
    // metres/second: knots = (speed_q/4) * 3600/1852, kept to one decimal.
    const uint32_t knots_e1 = (static_cast<uint32_t>(own.speed_q) * 2250u + 231u) / 463u;
    n += fmt_uint(out + n, knots_e1, 1, 1);
    out[n++] = ',';
    n += fmt_uint(out + n, to_degrees(Cordic9(own.track_c9)).v, 1);
    out[n++] = ',';
    n += fmt_uint(out + n, static_cast<uint32_t>(day), 2);
    n += fmt_uint(out + n, static_cast<uint32_t>(month), 2);
    n += fmt_uint(out + n, static_cast<uint32_t>(year % 100), 2);
    n += fmt_string(out + n, ",,,A");
    return nmea_finish(out, n);
}

int format_gpgga(char* out, size_t cap, const messages::OwnState& own) {
    (void)cap;
    if (!own.fix_valid || !own.utc_valid) return 0;
    int n = 0;
    n += fmt_string(out + n, "$GPGGA,");
    n += put_hhmmss(out + n, own.utc);
    out[n++] = ',';
    n += fmt_nmea_lat(out + n, own.lat_1e7);
    out[n++] = ',';
    n += fmt_nmea_lon(out + n, own.lon_1e7);
    n += fmt_string(out + n, ",1,");
    n += fmt_uint(out + n, own.sats, 2);
    out[n++] = ',';
    n += fmt_uint(out + n, own.hdop_e2, 3, 2);
    out[n++] = ',';
    n += fmt_int(out + n, own.alt_msl_m, 1, 0, true);
    n += fmt_string(out + n, ",M,");
    n += fmt_int(out + n, own.alt_m - own.alt_msl_m, 1, 0, true);
    n += fmt_string(out + n, ",M,,");
    return nmea_finish(out, n);
}

}
