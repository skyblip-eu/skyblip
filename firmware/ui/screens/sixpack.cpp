#include "ui/screens/sixpack.h"

#include "core/util/format.h"
#include "core/util/intmath.h"

namespace skyblip::ui {

namespace {

constexpr int kR = 29;
constexpr int kCx[3] = {34, 100, 166};
constexpr int kCy[2] = {56, 138};
constexpr int kTitleDy = -(kR + 10);
constexpr int kValueDy = kR + 4;
constexpr int kTickLen = 4;
constexpr int kTicks = 12;
constexpr int kNeedle = kR - 6;
constexpr int kShortNeedle = kR - 15;
constexpr int kHubR = 2;
constexpr int kCharW = 6;

// isin/icos are Q14 (16384 = 1.0) over a 65536-unit circle.
constexpr int32_t kOne = 16384;
constexpr int32_t kTurn = 65536;

constexpr int32_t kAsiFullScaleKt = 160;
constexpr int32_t kAsiSpanDeg = 300;
constexpr int32_t kAltHundredsPerTurn = 1000;
constexpr int32_t kAltThousandsPerTurn = 10000;
constexpr int32_t kVsiFullScaleFpm = 2000;
constexpr int32_t kVsiSpanDeg = 80;
constexpr int32_t kVsiZeroDeg = -90;
constexpr int32_t kPitchFullScaleDeg = 20;
constexpr int32_t kBankLimitDeg = 60;
// A standard-rate turn (3 deg/s) at typical light-aircraft speeds is ~30 deg of
// bank, where the coordinator's index marks sit.
constexpr int32_t kStandardRateMarkDeg = 30;
constexpr int kWingHalf = kR - 8;

int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

int16_t c16(int32_t deg) { return static_cast<int16_t>((deg * kTurn) / 360); }

void text_center(Framebuffer& fb, int cx, int y, const char* s, int scale = 1) {
    int n = 0;
    while (s[n]) n++;
    fb.draw_text(cx - (n * kCharW * scale) / 2, y, s, true, scale);
}

void value_center(Framebuffer& fb, int cx, int y, bool have, int32_t v, bool no_plus,
                  uint8_t min_digits = 1) {
    if (!have) {
        text_center(fb, cx, y, "---");
        return;
    }
    char buf[12];
    int n = fmt_int(buf, v, min_digits, 0, no_plus);
    buf[n] = 0;
    text_center(fb, cx, y, buf);
}

void dial(Framebuffer& fb, int cx, int cy, const char* title) {
    fb.circle(cx, cy, kR, true);
    for (int i = 0; i < kTicks; i++) {
        const int16_t a = c16(i * (360 / kTicks));
        const int32_t s = isin(a), c = icos(a);
        fb.line(cx + static_cast<int>((kR * s) / kOne), cy - static_cast<int>((kR * c) / kOne),
                cx + static_cast<int>(((kR - kTickLen) * s) / kOne),
                cy - static_cast<int>(((kR - kTickLen) * c) / kOne), true);
    }
    text_center(fb, cx, cy + kTitleDy, title);
}

void needle(Framebuffer& fb, int cx, int cy, int32_t deg, int len) {
    const int16_t a = c16(deg);
    const int32_t s = isin(a), c = icos(a);
    fb.line(cx, cy, cx + static_cast<int>((len * s) / kOne), cy - static_cast<int>((len * c) / kOne),
            true);
    fb.circle(cx, cy, kHubR, true, true);
}

// The ground is solid black below a horizon that pitches with the flight-path
// angle and rolls opposite to the bank, clipped to the dial.
void attitude(Framebuffer& fb, int cx, int cy, int32_t pitch_deg, int32_t bank_deg) {
    const int32_t off = (clampi(pitch_deg, -kPitchFullScaleDeg, kPitchFullScaleDeg) * kR) /
                        kPitchFullScaleDeg;
    const int16_t a = c16(clampi(bank_deg, -kBankLimitDeg, kBankLimitDeg));
    const int32_t s = isin(a), c = icos(a);
    for (int dx = -kR + 1; dx < kR; dx++) {
        const int h = static_cast<int>(isqrt<uint32_t>(static_cast<uint32_t>(kR * kR - dx * dx)));
        const int32_t hy = clampi(off - (dx * s) / c, -h, h);
        fb.vline(cx + dx, cy + static_cast<int>(hy), h - static_cast<int>(hy) + 1, true);
    }
    for (int i = 0; i < 6; i++) {  // aircraft reference, legible over sky or ground
        fb.set_pixel(cx - 11 + i, cy, !fb.get_pixel(cx - 11 + i, cy));
        fb.set_pixel(cx + 6 + i, cy, !fb.get_pixel(cx + 6 + i, cy));
    }
}

void turn_coordinator(Framebuffer& fb, int cx, int cy, int32_t bank_deg) {
    const int16_t a = c16(clampi(bank_deg, -kBankLimitDeg, kBankLimitDeg));
    const int32_t s = isin(a), c = icos(a);
    const int wx = static_cast<int>((kWingHalf * c) / kOne);
    const int wy = static_cast<int>((kWingHalf * s) / kOne);
    fb.line(cx - wx, cy - wy, cx + wx, cy + wy, true);
    fb.circle(cx, cy, kHubR, true, true);
    for (int sign = -1; sign <= 1; sign += 2) {
        const int16_t m = c16(sign * kStandardRateMarkDeg + 90);
        const int32_t ms = isin(m), mc = icos(m);
        fb.line(cx + static_cast<int>((kR * ms) / kOne), cy - static_cast<int>((kR * mc) / kOne),
                cx + static_cast<int>(((kR - kTickLen) * ms) / kOne),
                cy - static_cast<int>(((kR - kTickLen) * mc) / kOne), true);
    }
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

    dial(fb, kCx[0], kCy[0], "GS KT");
    if (s.have_data)
        needle(fb, kCx[0], kCy[0], (clampi(kt, 0, kAsiFullScaleKt) * kAsiSpanDeg) / kAsiFullScaleKt,
               kNeedle);
    value_center(fb, kCx[0], kCy[0] + kValueDy, s.have_data, kt, true);

    dial(fb, kCx[1], kCy[0], "ATT");
    if (s.have_data) attitude(fb, kCx[1], kCy[0], pitch, bank);
    value_center(fb, kCx[1], kCy[0] + kValueDy, s.have_data, pitch, false);

    dial(fb, kCx[2], kCy[0], "ALT FT");
    if (s.have_data) {
        const int32_t alt = s.alt_ft < 0 ? 0 : s.alt_ft;
        needle(fb, kCx[2], kCy[0], ((alt % kAltThousandsPerTurn) * 360) / kAltThousandsPerTurn,
               kShortNeedle);
        needle(fb, kCx[2], kCy[0], ((alt % kAltHundredsPerTurn) * 360) / kAltHundredsPerTurn,
               kNeedle);
    }
    value_center(fb, kCx[2], kCy[0] + kValueDy, s.have_data, s.alt_ft, true);

    dial(fb, kCx[0], kCy[1], "TURN");
    if (s.have_data) turn_coordinator(fb, kCx[0], kCy[1], bank);
    value_center(fb, kCx[0], kCy[1] + kValueDy, s.have_data, s.turn_dps, false);

    dial(fb, kCx[1], kCy[1], "HDG");
    text_center(fb, kCx[1], kCy[1] - kR + 2, "N");
    if (s.have_data) needle(fb, kCx[1], kCy[1], s.track_deg % 360, kNeedle);
    value_center(fb, kCx[1], kCy[1] + kValueDy, s.have_data, s.track_deg % 360, true, 3);

    dial(fb, kCx[2], kCy[1], "VS FPM");
    if (s.have_data)
        needle(fb, kCx[2], kCy[1],
               kVsiZeroDeg + (clampi(s.vs_fpm, -kVsiFullScaleFpm, kVsiFullScaleFpm) * kVsiSpanDeg) /
                                 kVsiFullScaleFpm,
               kNeedle);
    value_center(fb, kCx[2], kCy[1] + kValueDy, s.have_data, s.vs_fpm, false);
}

}  // namespace skyblip::ui
