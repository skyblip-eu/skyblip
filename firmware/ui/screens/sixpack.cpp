#include "ui/screens/sixpack.h"

#include "core/util/format.h"
#include "core/util/intmath.h"
#include "ui/widgets/skyship.h"

namespace skyblip::ui {

namespace {

constexpr int kR = 29;
constexpr int kCx[3] = {34, 100, 166};
constexpr int kCy[2] = {56, 138};
constexpr int kTitleDy = -(kR + 10);
constexpr int kValueDy = kR + 4;
constexpr int kTickLen = 4;
constexpr int kTicks = 12;
constexpr int kAltTicks = 10;  // one mark per 100 ft, so the hands line up with them
// Card letters stay upright whatever the card does: at 5x7 a turned glyph is a
// staircase of its own stroke width, and unreadable beats authentic.
constexpr int kCardR = kR - 7;
constexpr int kIndexTabLen = 5;
constexpr int kNeedle = kR - 6;
constexpr int kShortNeedle = kR - 15;
constexpr int kHubR = 2;
constexpr int kCharW = 6;
constexpr int kGlyphH = 7;

// isin/icos are Q14 (16384 = 1.0) over a 65536-unit circle.
constexpr int32_t kOne = 16384;
constexpr int32_t kTurn = 65536;

constexpr int32_t kAsiFullScaleKt = 160;
// 300 km/h is 162 kt: the same arc for the same aeroplane, so a pilot who
// switches units keeps the needle position they learned.
constexpr int32_t kAsiFullScaleKmh = 300;
constexpr int32_t kAsiSpanDeg = 300;
// A metric altimeter is graduated 100 m to the mark and 1000 m to the turn of
// the long hand, which is the same three-pointer geometry as 100 ft and
// 1000 ft. Only the number under it changes.
constexpr int32_t kAltHundredsPerTurn = 1000;
constexpr int32_t kAltThousandsPerTurn = 10000;
constexpr int32_t kVsiFullScaleFpm = 2000;
// Tenths of a metre per second: +-10 m/s is the 2000 fpm arc, and a vario is
// read to the decimal.
constexpr int32_t kVsiFullScaleDmps = 100;
constexpr int32_t kVsiSpanDeg = 80;
constexpr int32_t kVsiZeroDeg = -90;
constexpr int32_t kPitchFullScaleDeg = 20;
constexpr int32_t kBankLimitDeg = 60;
// A standard-rate turn (3 deg/s) at typical light-aircraft speeds is ~30 deg of
// bank, where the coordinator's index marks sit.
constexpr int32_t kStandardRateMarkDeg = 30;
constexpr int kWingHalf = kR - 8;

int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Rounded, sign-symmetric projection of a radius onto a cordic axis: plain
// integer division truncates toward zero and pulls both ends of a mark inward.
int radial_half(int32_t r_half, int32_t q14) {
    const int32_t v = r_half * q14;
    return static_cast<int>((v + (v < 0 ? -kOne : kOne)) / (2 * kOne));
}

int radial(int32_t r, int32_t q14) { return radial_half(2 * r, q14); }

int16_t c16(int32_t deg) {
    int32_t d = ((deg % 360) + 360) % 360;
    if (d >= 180) d -= 360;  // keep the cordic value inside int16_t
    return static_cast<int16_t>((d * kTurn) / 360);
}

void text_center(Framebuffer& fb, int cx, int y, const char* s, int scale = 1) {
    int n = 0;
    while (s[n]) n++;
    fb.draw_text(cx - (n * kCharW * scale) / 2, y, s, true, scale);
}

void value_center(Framebuffer& fb, int cx, int y, bool have, int32_t v, bool no_plus,
                  uint8_t min_digits = 1, uint8_t decimals = 0) {
    if (!have) {
        text_center(fb, cx, y, "---");
        return;
    }
    char buf[12];
    int n = fmt_int(buf, v, min_digits, decimals, no_plus);
    buf[n] = 0;
    text_center(fb, cx, y, buf);
}

// Marks are stepped along the true radius in half-pixels rather than drawn as a
// line between two rounded end points: over 4 px, rounding both ends tilts the
// mark by degrees, which is what made the scales look bent.
void tick(Framebuffer& fb, int cx, int cy, int16_t a, int len = kTickLen) {
    const int32_t s = isin(a), c = icos(a);
    for (int half = 2 * (kR - len); half <= 2 * kR; half++) {
        fb.set_pixel(cx + radial_half(half, s), cy - radial_half(half, c), true);
    }
}

void dial(Framebuffer& fb, int cx, int cy, const char* title, int ticks = kTicks) {
    fb.circle(cx, cy, kR, true);
    for (int i = 0; i < ticks; i++) tick(fb, cx, cy, c16(i * (360 / ticks)));
    text_center(fb, cx, cy + kTitleDy, title);
}

void needle(Framebuffer& fb, int cx, int cy, int32_t deg, int len, bool thick = false) {
    const int16_t a = c16(deg);
    const int32_t s = isin(a), c = icos(a);
    const int tx = cx + radial(len, s);
    const int ty = cy - radial(len, c);
    fb.line(cx, cy, tx, ty, true);
    if (thick) {  // the altimeter's thousands hand: short and broad, hundreds long and fine
        fb.line(cx + 1, cy, tx + 1, ty, true);
        fb.line(cx, cy + 1, tx, ty + 1, true);
    }
    fb.circle(cx, cy, kHubR, true, true);
}

// The ground is a 50% checkerboard below a horizon that pitches with the
// flight-path angle and rolls opposite to the bank, clipped to the dial. On a
// 1-bit panel that alternation is the only grey there is, and it keeps the
// attitude dial from being the one black hole on the instrument face.
void attitude(Framebuffer& fb, int cx, int cy, int32_t pitch_deg, int32_t bank_deg) {
    const int32_t off =
        (clampi(pitch_deg, -kPitchFullScaleDeg, kPitchFullScaleDeg) * kR) / kPitchFullScaleDeg;
    const int16_t a = c16(clampi(bank_deg, -kBankLimitDeg, kBankLimitDeg));
    const int32_t s = isin(a), c = icos(a);
    for (int dx = -kR + 1; dx < kR; dx++) {
        const int h = static_cast<int>(isqrt<uint32_t>(static_cast<uint32_t>(kR * kR - dx * dx)));
        const int32_t hy = clampi(off - (dx * s) / c, -h, h);
        const int x = cx + dx;
        for (int y = cy + static_cast<int>(hy); y <= cy + h; y++) {
            if (((x + y) & 1) == 0) fb.set_pixel(x, y, true);
        }
    }
    for (int i = 0; i < 6; i++) {  // aircraft reference, solid over sky or ground
        fb.set_pixel(cx - 11 + i, cy, true);
        fb.set_pixel(cx + 6 + i, cy, true);
    }
}

void turn_coordinator(Framebuffer& fb, int cx, int cy, int32_t bank_deg) {
    const int16_t a = c16(clampi(bank_deg, -kBankLimitDeg, kBankLimitDeg));
    const int32_t s = isin(a), c = icos(a);
    const int wx = radial(kWingHalf, c);
    const int wy = radial(kWingHalf, s);
    fb.line(cx - wx, cy - wy, cx + wx, cy + wy, true);
    fb.circle(cx, cy, kHubR, true, true);
    for (int sign = -1; sign <= 1; sign += 2)
        tick(fb, cx, cy, c16(sign * kStandardRateMarkDeg + 90));
}

// The card rotates so the flown track sits under the fixed index at the top,
// which is what makes N/E/S/W read as directions rather than as labels.
void heading_card(Framebuffer& fb, int cx, int cy, int32_t track_deg) {
    static const char kCardinal[4] = {'N', 'E', 'S', 'W'};
    for (int i = 0; i < kTicks; i++) {
        const int32_t bearing = i * (360 / kTicks);
        const int16_t a = c16(bearing - track_deg);
        if (bearing % 90 != 0) {
            tick(fb, cx, cy, a);
            continue;
        }
        fb.draw_char(cx - kCharW / 2 + radial(kCardR, isin(a)),
                     cy - kGlyphH / 2 - radial(kCardR, icos(a)), kCardinal[bearing / 90], true);
    }
    fb.rect(cx - 1, cy - kR, 3, kIndexTabLen, true, true);  // the lubber index, on the case
    draw_skyship(fb, cx, cy);
}

// 1 kt = 101.3 ft/min, so tan(flight-path angle) = vs_fpm / (101.3 * kt).
int32_t flight_path_deg(int32_t vs_fpm, int32_t speed_kt) {
    if (speed_kt <= 0) return 0;
    return (static_cast<int32_t>(iatan2(vs_fpm * 10, speed_kt * 1013)) * 360) / kTurn;
}

// Coordinated turn: tan(bank) = omega * V / g, which in deg/s and knots is
// turn_dps * kt / 1093.
int32_t bank_deg(int32_t turn_dps, int32_t speed_kt) {
    if (speed_kt <= 0) return 0;
    return (static_cast<int32_t>(iatan2(turn_dps * speed_kt, 1093)) * 360) / kTurn;
}

}  // namespace

void draw_sixpack(Framebuffer& fb, const SixPackSnapshot& s) {
    fb.clear(true);

    const int32_t kt = clampi(s.speed_kt, 0, 999);
    const int32_t pitch = flight_path_deg(s.vs_fpm, kt);
    const int32_t bank = bank_deg(s.turn_dps, kt);

    const bool metric = s.units == settings::Units::Metric;
    const int32_t speed = metric ? (kt * 1852) / 1000 : kt;
    const int32_t speed_full = metric ? kAsiFullScaleKmh : kAsiFullScaleKt;
    const int32_t alt = metric ? (s.alt_ft * 3048) / 10000 : s.alt_ft;
    const int32_t vs = metric ? (s.vs_fpm * 508) / 10000 : s.vs_fpm;
    const int32_t vs_full = metric ? kVsiFullScaleDmps : kVsiFullScaleFpm;

    dial(fb, kCx[0], kCy[0], metric ? "GS KM/H" : "GS KT");
    if (s.have_data)
        needle(fb, kCx[0], kCy[0], (clampi(speed, 0, speed_full) * kAsiSpanDeg) / speed_full,
               kNeedle);
    value_center(fb, kCx[0], kCy[0] + kValueDy, s.have_data, speed, true);

    dial(fb, kCx[1], kCy[0], "ATT");
    if (s.have_data) attitude(fb, kCx[1], kCy[0], pitch, bank);
    value_center(fb, kCx[1], kCy[0] + kValueDy, s.have_data, pitch, false);

    dial(fb, kCx[2], kCy[0], metric ? "ALT M" : "ALT FT", kAltTicks);
    if (s.have_data) {
        const int32_t on_scale = alt < 0 ? 0 : alt;
        needle(fb, kCx[2], kCy[0], ((on_scale % kAltThousandsPerTurn) * 360) / kAltThousandsPerTurn,
               kShortNeedle,
               /*thick=*/true);
        needle(fb, kCx[2], kCy[0], ((on_scale % kAltHundredsPerTurn) * 360) / kAltHundredsPerTurn,
               kNeedle);
    }
    value_center(fb, kCx[2], kCy[0] + kValueDy, s.have_data, alt, true);

    dial(fb, kCx[0], kCy[1], "TURN");
    if (s.have_data) turn_coordinator(fb, kCx[0], kCy[1], bank);
    value_center(fb, kCx[0], kCy[1] + kValueDy, s.have_data, s.turn_dps, false);

    // The card shows GNSS TRACK, not heading: there is no magnetometer, and
    // labelling it HDG would claim a sensor the board does not have.
    char track[12];
    int n = fmt_string(track, "TRK ");
    if (s.have_data)
        n += fmt_uint(track + n, s.track_deg % 360, 3);
    else
        n += fmt_string(track + n, "---");
    track[n] = 0;
    dial(fb, kCx[1], kCy[1], track, s.have_data ? 0 : kTicks);
    if (s.have_data) heading_card(fb, kCx[1], kCy[1], s.track_deg % 360);

    dial(fb, kCx[2], kCy[1], metric ? "VS M/S" : "VS FPM");
    if (s.have_data)
        needle(fb, kCx[2], kCy[1],
               kVsiZeroDeg + (clampi(vs, -vs_full, vs_full) * kVsiSpanDeg) / vs_full, kNeedle);
    value_center(fb, kCx[2], kCy[1] + kValueDy, s.have_data, vs, false, 1, metric ? 1 : 0);
}

}  // namespace skyblip::ui
