#include "ui/screens/status.h"

#include "core/util/format.h"
#include "core/util/units.h"

namespace skyblip::ui {

namespace {
constexpr int kLeft = 4;
constexpr int kLineH = 15;

// Draw "LABEL  value" on one row.
void row(Framebuffer& fb, int y, const char* label, const char* value) {
    fb.draw_text(kLeft, y, label, true, 1);
    fb.draw_text(kLeft + 56, y, value, true, 1);
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

    int y = 27;

    // GNSS fix + satellites.
    n = fmt_string(buf, s.fix_valid ? "3D " : "-- ");
    n += fmt_uint(buf + n, s.sats, 2);
    n += fmt_string(buf + n, " sat");
    buf[n] = 0;
    row(fb, y, "FIX", buf);
    y += kLineH;

    if (s.fix_valid) {
        n = fmt_int(buf, s.lat_1e7, 1, 7, true);
        buf[n] = 0;
        row(fb, y, "LAT", buf);
        y += kLineH;

        n = fmt_int(buf, s.lon_1e7, 1, 7, true);
        buf[n] = 0;
        row(fb, y, "LON", buf);
        y += kLineH;
    } else {
        row(fb, y, "POS", "no fix");
        y += kLineH * 2;
    }

    // Altitude (m or ft per settings).
    if (s.imperial) {
        n = fmt_int(buf, to_feet(Metres(s.alt_m)).v, 1, 0, true);
        n += fmt_string(buf + n, " ft");
    } else {
        n = fmt_int(buf, s.alt_m, 1, 0, true);
        n += fmt_string(buf + n, " m");
    }
    buf[n] = 0;
    row(fb, y, "ALT", buf);
    y += kLineH;

    // Ground speed (m/s) and vertical speed (m/s, one decimal).
    n = fmt_uint(buf, static_cast<uint32_t>(s.speed_q) / 4, 1);
    n += fmt_string(buf + n, " m/s");
    buf[n] = 0;
    row(fb, y, "SPD", buf);
    y += kLineH;

    n = fmt_int(buf, (static_cast<int32_t>(s.climb_e8) * 10) / 8, 1, 1, false);
    n += fmt_string(buf + n, " m/s");
    buf[n] = 0;
    row(fb, y, "VS", buf);
    y += kLineH;

    // Track in degrees.
    n = fmt_uint(buf, to_degrees(Cordic9(s.track_c9)).v, 3);
    buf[n] = 0;
    row(fb, y, "TRK", buf);
    y += kLineH;

    // UTC as hh:mm:ss (seconds-of-day from the fix).
    if (s.utc_valid) {
        uint32_t sod = s.utc % 86400u;
        n = fmt_uint(buf, sod / 3600u, 2);
        n += fmt_string(buf + n, ":");
        n += fmt_uint(buf + n, (sod / 60u) % 60u, 2);
        n += fmt_string(buf + n, ":");
        n += fmt_uint(buf + n, sod % 60u, 2);
        buf[n] = 0;
    } else {
        n = fmt_string(buf, "--:--:--");
        buf[n] = 0;
    }
    row(fb, y, "UTC", buf);
    y += kLineH;

    // Traffic count + PPS lock (clock health drives TX permission).
    n = fmt_uint(buf, static_cast<uint32_t>(s.n_targets), 1);
    n += fmt_string(buf + n, s.pps_locked ? "  PPS ok" : "  PPS --");
    buf[n] = 0;
    row(fb, y, "TFC", buf);
}

}  // namespace skyblip::ui
