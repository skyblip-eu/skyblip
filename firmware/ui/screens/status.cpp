#include "ui/screens/status.h"

#include "core/util/format.h"
#include "core/util/units.h"

namespace skyblip::ui {

namespace {
constexpr int kLeft = 4;
constexpr int kLineH = 16;
constexpr int kCellW = 6;  // the 5x7 font's advance at scale 1
constexpr int kColumn(int cell) { return kLeft + cell * kCellW; }

// One grid, in character cells: 4 label, 1 space, 5 value, 4 unit, 3 space,
// 5 value, 4 unit. Every unit string carries its own leading space, which is
// what keeps a five-character value off it. km/h then runs one cell past the
// field, which is what it costs to have every unit start in the same column.
constexpr int kValueX = kColumn(5);
constexpr int kAeroNumberEnd = kColumn(10);
constexpr int kAeroUnitX = kColumn(10);
constexpr int kAeroUnitEnd = kColumn(15);  // past a leading space and four characters
constexpr int kSiNumberEnd = kColumn(22);
constexpr int kSiUnitX = kColumn(22);

// 1 m/s = 1.94384 kt, from quarter-m/s.
int32_t knots(uint16_t speed_q) { return (static_cast<int32_t>(speed_q) * 194384) / (4 * 100000); }

// 1 m/s = 3.6 km/h, from quarter-m/s.
int32_t kilometres_per_hour(uint16_t speed_q) {
    return (static_cast<int32_t>(speed_q) * 36) / (4 * 10);
}

// 1 m/s = 196.85 ft/min, from eighth-m/s.
int32_t feet_per_minute(int16_t climb_e8) {
    return (static_cast<int32_t>(climb_e8) * 19685) / (8 * 100);
}

// Draw "LABEL  value" on one row.
void row(Framebuffer& fb, int y, const char* label, const char* value) {
    fb.draw_text(kLeft, y, label, true, 1);
    fb.draw_text(kValueX, y, value, true, 1);
}

void right_aligned(Framebuffer& fb, int x_end, int y, const char* text, int len) {
    fb.draw_text(x_end - len * kCellW, y, text, true, 1);
}

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// The same two columns as the numbers, for the rows whose values are words: the
// value right-aligned on the column, its unit left-aligned after it.
void text_row(Framebuffer& fb, int y, const char* label, const char* value, const char* unit,
              const char* value2 = "", const char* unit2 = "") {
    fb.draw_text(kLeft, y, label, true, 1);
    right_aligned(fb, kAeroNumberEnd, y, value, length(value));
    fb.draw_text(kAeroUnitX, y, unit, true, 1);
    right_aligned(fb, kSiNumberEnd, y, value2, length(value2));
    fb.draw_text(kSiUnitX, y, unit2, true, 1);
}

struct Quantity {
    int32_t value{0};
    uint8_t decimals{0};
    const char* unit{""};
};

// Aeronautical unit in the first column, SI in the second: the pilot reads the
// left, the engineer checks the right.
void dual_row(Framebuffer& fb, int y, const char* label, Quantity aero, Quantity si, bool no_plus) {
    char buf[16];
    fb.draw_text(kLeft, y, label, true, 1);

    int n = fmt_int(buf, aero.value, 1, aero.decimals, no_plus);
    buf[n] = 0;
    right_aligned(fb, kAeroNumberEnd, y, buf, n);
    fb.draw_text(kAeroUnitX, y, aero.unit, true, 1);

    n = fmt_int(buf, si.value, 1, si.decimals, no_plus);
    buf[n] = 0;
    right_aligned(fb, kSiNumberEnd, y, buf, n);
    fb.draw_text(kSiUnitX, y, si.unit, true, 1);
}

// Both pressures on one line, in the two number columns: what the sensor reads,
// and the subscale the altitudes below are read against. Neither is aero-vs-SI,
// so they share the one unit.
void pressure_row(Framebuffer& fb, int y, uint32_t pressure_pa, uint32_t qnh_pa) {
    // Whole hectopascals: five cells hold no decimal, and a subscale is set in
    // whole hPa anyway.
    char baro[8];
    int n = fmt_uint(baro, (pressure_pa + 50) / 100, 1);
    baro[n] = 0;

    // "QNH 1013" is three cells wider than the value field, so it grows left
    // into the gap and the barometer's own unit gives up its cells: one hPa at
    // the end serves both numbers, and it lands in the unit column with every
    // other unit on the page.
    char qnh[12];
    n = fmt_string(qnh, "QNH ");
    if (qnh_pa != 0)
        n += fmt_uint(qnh + n, (qnh_pa + 50) / 100, 1);
    else
        n += fmt_string(qnh + n, "----");
    qnh[n] = 0;

    text_row(fb, y, "BARO", baro, "", qnh, " hPa");
}
}  // namespace

void draw_status(Framebuffer& fb, const StatusSnapshot& s) {
    fb.clear(true);

    char buf[32];

    // Header: device address (the ADS-L identity) + fix state.
    int n = fmt_string(buf, "ID ");
    n += fmt_hex(buf + n, s.device_addr, 6);
    buf[n] = 0;
    fb.draw_text(kLeft, 3, buf, true, 2);
    fb.hline(kLeft, 21, Framebuffer::kW - 2 * kLeft, true);

    // Everything that is one free-form string first, then the block where every
    // number lines up in its column.
    int y = 27;

    // Fix state and the clock it comes from, on one line: both answer "is the
    // GNSS working". A count with no fix behind it is dashes, which is the same
    // bit the fix flag carries.
    char sats[4] = {'-', '-', 0, 0};
    if (s.fix_valid) {
        n = fmt_uint(sats, s.sats, 2);
        sats[n] = 0;
    }
    // Four satellites are the fewest that can solve for altitude, so a fix on
    // three is a 2D one whatever the receiver calls it.
    const char* mode = !s.fix_valid ? "--" : (s.sats >= 4 ? "3D" : "2D");

    // hh:mm:ss, the seconds-of-day from that fix.
    n = fmt_string(buf, " ");
    if (s.utc_valid) {
        uint32_t sod = s.utc % 86400u;
        n += fmt_uint(buf + n, sod / 3600u, 2);
        n += fmt_string(buf + n, ":");
        n += fmt_uint(buf + n, (sod / 60u) % 60u, 2);
        n += fmt_string(buf + n, ":");
        n += fmt_uint(buf + n, sod % 60u, 2);
    } else {
        n += fmt_string(buf + n, "--:--:--");
    }
    buf[n] = 0;
    text_row(fb, y, "FIX", sats, " SAT", "UTC", buf);
    fb.draw_text(kValueX, y, mode, true, 1);
    y += kLineH;

    // Full 1e-7 degrees on both, which is what the fix carries. A signed
    // three-digit longitude is then 16 cells wide with its label, so the
    // longitude block is anchored to the right margin rather than to a column:
    // at seven decimals nothing narrower fits every position on earth.
    if (s.fix_valid) {
        // Ten characters of latitude do not fit a five-cell value field, so it
        // ends where the first column's unit does: level with TRUE above it.
        n = fmt_int(buf, s.lat_1e7, 1, 7, true);
        buf[n] = 0;
        fb.draw_text(kLeft, y, "LAT", true, 1);
        right_aligned(fb, kAeroUnitEnd, y, buf, n);

        n = fmt_string(buf, "LON ");
        n += fmt_int(buf + n, s.lon_1e7, 1, 7, true);
        buf[n] = 0;
        right_aligned(fb, Framebuffer::kW - kLeft, y, buf, n);
    } else {
        row(fb, y, "LAT", "no fix");
    }
    y += kLineH;

    n = fmt_uint(buf, to_degrees(Cordic9(s.track_c9)).v, 3);
    buf[n] = 0;
    text_row(fb, y, "TRK", buf, " TRUE");
    y += kLineH;

    // Traffic count, and the PPS lock that decides whether we may transmit.
    char count[8];
    n = fmt_uint(count, static_cast<uint32_t>(s.n_targets), 1);
    count[n] = 0;
    text_row(fb, y, "TFC", count, "", "PPS", s.pps_locked ? " OK" : " --");
    y += kLineH;

    // The aligned block: the pressures the altitudes depend on, then altitude on
    // the subscale that was set, the geometric one, and pressure altitude on the
    // 1013.25 hPa standard setting - the one a flight level counts in hundreds
    // of feet. Then the motion pair.
    if (s.baro_valid) {
        pressure_row(fb, y, s.pressure_pa, s.qnh_pa);
        y += kLineH;

        dual_row(fb, y, "ALT", {to_feet(Metres(s.alt_qnh_m)).v, 0, " ft"}, {s.alt_qnh_m, 0, " m"},
                 true);
        y += kLineH;
    } else {
        row(fb, y, "BARO", "no sensor");
        y += kLineH * 2;
    }

    dual_row(fb, y, "GNSS", {to_feet(Metres(s.alt_m)).v, 0, " ft"}, {s.alt_m, 0, " m"}, true);
    y += kLineH;

    if (s.baro_valid)
        dual_row(fb, y, "STD", {to_feet(Metres(s.alt_std_m)).v, 0, " ft"}, {s.alt_std_m, 0, " m"},
                 true);
    y += kLineH;

    dual_row(fb, y, "SPD", {knots(s.speed_q), 0, " kt"},
             {kilometres_per_hour(s.speed_q), 0, " km/h"}, true);
    y += kLineH;

    dual_row(fb, y, "VS", {feet_per_minute(s.climb_e8), 0, " fpm"},
             {(static_cast<int32_t>(s.climb_e8) * 10) / 8, 1, " m/s"}, false);
}

}  // namespace skyblip::ui
