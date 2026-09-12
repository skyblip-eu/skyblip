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
constexpr int kOuterR = 92;
constexpr int kRingEdge = kOuterR - 1;
constexpr int kNoFixY = 150;
constexpr int kMarginX = 4;
constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;
constexpr int kCellW = 6;
constexpr int kLabelPad = 2;
constexpr int kAlarmBarH = 3;
constexpr int kHeadingScale = 2;
constexpr int kHeadingCy = kAlarmBarH + kLabelPad + kGlyphH;
constexpr int kHeadingLabelBottom = kHeadingCy + (kGlyphH * kHeadingScale) / 2 + kLabelPad;
constexpr int kCardinalR = kFar - kHeadingLabelBottom - kLabelPad - kGlyphH / 2;
constexpr int kFooterCy = kFar + kRingEdge;
constexpr int kFooterY = kFooterCy - kGlyphH / 2;
constexpr int32_t kQ14One = 16384;
constexpr int32_t kTurn16 = 65536;

// A ring symmetric about the centre POINT: the midpoint algorithm's octant is
// mirrored onto the four (kNear|kFar) anchors instead of a single centre pixel,
// so the ring has the same margin on all four sides (8 px) - fb.circle() would
// put 8 one side and 7 the other.
void ring(Framebuffer& fb, int r) {
    int x = r - 1, y = 0, dx = 1, dy = 1, err = dx - 2 * r;
    while (x >= y) {
        for (int i = 0; i < 2; i++) {  // (x,y) and its transpose
            int a = i ? y : x, b = i ? x : y;
            fb.set_pixel(kFar + a, kFar + b, true);
            fb.set_pixel(kNear - a, kFar + b, true);
            fb.set_pixel(kFar + a, kNear - b, true);
            fb.set_pixel(kNear - a, kNear - b, true);
        }
        if (err <= 0) {
            y++;
            err += dy;
            dy += 2;
        }
        if (err > 0) {
            x--;
            dx += 2;
            err += dx - 2 * r;
        }
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

void ring_label(Framebuffer& fb, int cy, const char* s, int scale) {
    const int w = text_width(s, scale), h = kGlyphH * scale;
    fb.rect(kCx - w / 2 - kLabelPad, cy - h / 2 - kLabelPad, w + 2 * kLabelPad, h + 2 * kLabelPad,
            false, true);
    text_center(fb, kCx, cy - h / 2, s, scale);
}

void cardinals(Framebuffer& fb, int16_t track) {
    static const struct {
        char letter;
        int32_t north, east;
    } kPoints[4] = {{'N', 1, 0}, {'E', 0, 1}, {'S', -1, 0}, {'W', 0, -1}};
    for (const auto& p : kPoints) {
        const HeadingUp at = heading_up(p.north * kCardinalR, p.east * kCardinalR, track);
        fb.draw_char(px_of(at.right) - kGlyphW / 2, py_of(at.ahead) - kGlyphH / 2, p.letter, true,
                     1);
    }
}

void heading_label(Framebuffer& fb, const RadarSnapshot& snap) {
    char buf[8];
    const int n = snap.have_fix ? fmt_uint(buf, snap.track_deg % 360, 3) : fmt_string(buf, "---");
    buf[n] = 0;
    ring_label(fb, kHeadingCy, buf, kHeadingScale);
}

void range_label(Framebuffer& fb, int32_t range_m) {
    char buf[12];
    int n = fmt_uint(buf, static_cast<uint32_t>((range_m + 50) / 100), 1, 1);
    n += fmt_string(buf + n, " KM");
    buf[n] = 0;
    ring_label(fb, kFooterCy, buf, 1);
}

void counts(Framebuffer& fb, uint8_t sats, int in_view) {
    char buf[12];
    int n = fmt_uint(buf, static_cast<uint32_t>(in_view));
    n += fmt_string(buf + n, " AC");
    buf[n] = 0;
    fb.draw_text(kMarginX, kFooterY, buf, true, 1);

    n = fmt_uint(buf, sats);
    n += fmt_string(buf + n, " SAT");
    buf[n] = 0;
    fb.draw_text(Framebuffer::kW - kMarginX - text_width(buf, 1), kFooterY, buf, true, 1);
}

int plot(Framebuffer& fb, const RadarSnapshot& snap, int16_t track) {
    const int64_t range = snap.range_m > 0 ? snap.range_m : 1;
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
    ring(fb, kOuterR / 2);

    const int16_t track = c16(snap.track_deg);
    if (snap.have_fix) cardinals(fb, track);

    draw_skyship(fb, kFar, kNear);

    fb.draw_text(2, 2, snap.coverage ? "U" : "-", true, 1);

    int in_view = 0;
    if (snap.have_fix)
        in_view = plot(fb, snap, track);
    else
        text_center(fb, kCx, kNoFixY, "NO FIX");

    heading_label(fb, snap);
    range_label(fb, snap.range_m);
    counts(fb, snap.sats, in_view);

    if (snap.max_alarm >= 3) fb.rect(0, 0, Framebuffer::kW, kAlarmBarH, true, true);
}

}  // namespace skyblip::ui
