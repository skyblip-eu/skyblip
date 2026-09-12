#include "ui/screens/radar.h"

#include "core/util/format.h"
#include "core/util/intmath.h"
#include "ui/widgets/skyship.h"

namespace skyblip::ui {

namespace {
// The screen is 200x200, an EVEN grid: there is no middle pixel. The centre is
// the POINT where four pixels meet, so each axis has a near-side and a far-side
// middle pixel - 99 and 100. Everything on this screen is built around that
// point rather than around a pixel:
//
//   kNear = 99   the pixel just before the centre (left, and above)
//   kFar  = 100  the pixel just after it (right, and below)
//
// A feature at distance d from the centre therefore occupies kNear-(d-1) on one
// side and kFar+(d-1) on the other. A feature ON the centre is a PAIR of
// pixels, never one. That makes the own ship exactly centred (its fuselage
// straddles 99|100), the rings exactly concentric with it, and every target
// offset measured from the same point in both directions.
constexpr int kNear = Framebuffer::kW / 2 - 1;
constexpr int kFar = Framebuffer::kW / 2;
constexpr int kCx = kFar;  // only for centring text, which has no such nicety
constexpr int kMargin = 4;
constexpr int kOuterR = 92;
constexpr int kRingW = 2;
constexpr int kGlyphH = 7;
constexpr int kCellW = 6;
constexpr int kAlarmBarH = 3;
constexpr int kTrackScale = 3;
constexpr int kRangeScale = 2;
constexpr int kSatScale = kRangeScale;
constexpr int kTrafficScale = kTrackScale;
constexpr int kTrackPad = 8;
constexpr int kRangePad = 2;
constexpr int kFooterBottom = Framebuffer::kH - kMargin;
constexpr int kFooterY = kFooterBottom - kGlyphH;
constexpr int kTrackY = kFooterBottom - kGlyphH * kTrackScale;
constexpr int kSatY = kFooterBottom - kGlyphH * kSatScale;
constexpr int kTrafficY = kFooterBottom - kGlyphH * kTrafficScale;
constexpr int kRangeY = kSatY - kMargin - kGlyphH * kRangeScale;
constexpr int kUnitGap = 3;
constexpr int32_t kQ14One = 16384;
constexpr int32_t kTurn16 = 65536;

int half_chord_in_half_pixels(int r, int b) {
    const int32_t v = 4 * r * r - (2 * b + 1) * (2 * b + 1);
    if (v < 0) return -1;
    int q = isqrt<int32_t>(v);
    while (q * q > v) q--;
    return (q - 1) / 2;
}

void ring(Framebuffer& fb, int r) {
    for (int b = 0; b < r; b++) {
        const int outer = half_chord_in_half_pixels(r, b);
        if (outer < 0) continue;
        const int inner = half_chord_in_half_pixels(r - kRingW, b) + 1;
        const int w = outer - inner + 1;
        fb.hline(kFar + inner, kFar + b, w, true);
        fb.hline(kFar + inner, kNear - b, w, true);
        fb.hline(kNear - outer, kFar + b, w, true);
        fb.hline(kNear - outer, kNear - b, w, true);
    }
}

int16_t c16(int32_t deg) {
    int32_t d = ((deg % 360) + 360) % 360;
    if (d >= 180) d -= 360;  // keep the cordic value inside int16_t
    return static_cast<int16_t>((d * kTurn16) / 360);
}

struct HeadingUp {
    int32_t ahead;
    int32_t right;
};

HeadingUp heading_up(int32_t north, int32_t east, int16_t track) {
    const int64_t c = icos(track), s = isin(track);
    return {static_cast<int32_t>((north * c + east * s) / kQ14One),
            static_cast<int32_t>((east * c - north * s) / kQ14One)};
}

int px_of(int32_t right) { return right >= 0 ? kFar + right : kNear + right + 1; }
int py_of(int32_t ahead) { return ahead <= 0 ? kFar - ahead : kNear - ahead + 1; }

int text_width(const char* s, int scale) {
    int n = 0;
    while (s[n]) n++;
    return n * kCellW * scale - scale;
}

void text_center(Framebuffer& fb, int cx, int y, const char* s, int scale = 1) {
    fb.draw_text(cx - text_width(s, scale) / 2, y, s, true, scale);
}

void clear_behind(Framebuffer& fb, int x, int y, int w, int h, int pad) {
    fb.rect(x - pad, y - pad, w + 2 * pad, h + 2 * pad, false, true);
}

void track_label(Framebuffer& fb, const RadarSnapshot& snap) {
    char buf[8];
    const int n = snap.have_fix ? fmt_uint(buf, snap.track_deg % 360, 3) : fmt_string(buf, "---");
    buf[n] = 0;
    const int w = text_width(buf, kTrackScale);
    clear_behind(fb, kCx - w / 2, kTrackY, w, kGlyphH * kTrackScale, kTrackPad);
    text_center(fb, kCx, kTrackY, buf, kTrackScale);
}

void unit_count(Framebuffer& fb, int x, int y, const char* number, int scale, const char* unit) {
    const int w = text_width(number, scale);
    fb.draw_text(x, y, number, true, scale);
    fb.draw_text(x + w + kUnitGap, y + kGlyphH * (scale - 1), unit, true, 1);
}

void range_label(Framebuffer& fb, int32_t range_nm) {
    char buf[8];
    buf[fmt_uint(buf, static_cast<uint32_t>(range_nm))] = 0;
    const int w = text_width(buf, kRangeScale) + kUnitGap + text_width("NM", 1);
    clear_behind(fb, kMargin, kRangeY, w, kGlyphH * kRangeScale, kRangePad);
    unit_count(fb, kMargin, kRangeY, buf, kRangeScale, "NM");
}

void satellites(Framebuffer& fb, uint8_t sats, bool have_fix) {
    char buf[4];
    buf[fmt_uint(buf, sats)] = 0;
    unit_count(fb, kMargin, kSatY, buf, kSatScale, have_fix ? "SAT" : "NO FIX");
}

void aircraft(Framebuffer& fb, int in_view) {
    char buf[4];
    buf[fmt_uint(buf, static_cast<uint32_t>(in_view))] = 0;
    const int x = Framebuffer::kW - kMargin - text_width(buf, kTrafficScale);
    fb.draw_text(x, kTrafficY, buf, true, kTrafficScale);
    fb.draw_text(x - kUnitGap - text_width("ACT", 1), kFooterY, "ACT", true, 1);
}

int plot(Framebuffer& fb, const RadarSnapshot& snap, int16_t track) {
    const int64_t range = (snap.range_nm > 0 ? snap.range_nm : 1) * kMetresPerNm;
    int in_view = 0;
    for (int i = 0; i < snap.n_targets; i++) {
        const RadarTarget& t = snap.targets[i];
        const HeadingUp at = heading_up(t.north_m, t.east_m, track);
        const int dx = static_cast<int>((static_cast<int64_t>(at.right) * kOuterR) / range);
        const int dy = static_cast<int>((static_cast<int64_t>(at.ahead) * kOuterR) / range);
        if (dx * dx + dy * dy > kOuterR * kOuterR) continue;
        const int px = px_of(dx), py = py_of(dy);
        const int r = t.alarm_level >= 2 ? 4 : 2;
        fb.circle(px, py, r, true, t.alarm_level >= 3);
        if (t.up_m > 30)
            fb.vline(px, py - r - 3, 2, true);
        else if (t.up_m < -30)
            fb.vline(px, py + r + 1, 2, true);
        in_view++;
    }
    return in_view;
}

}  // namespace

void draw_radar(Framebuffer& fb, const RadarSnapshot& snap) {
    fb.clear(true);

    ring(fb, kOuterR);

    const int16_t track = c16(snap.track_deg);

    draw_skyship(fb, kFar, kNear);

    const int in_view = snap.have_fix ? plot(fb, snap, track) : 0;

    range_label(fb, snap.range_nm);
    track_label(fb, snap);
    satellites(fb, snap.sats, snap.have_fix);
    aircraft(fb, in_view);

    if (snap.max_alarm >= 3) fb.rect(0, 0, Framebuffer::kW, kAlarmBarH, true, true);
}

}  // namespace skyblip::ui
