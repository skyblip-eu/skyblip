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
constexpr int kMarginX = 4;
constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;
constexpr int kCellW = 6;
constexpr int kLabelPad = 2;
constexpr int kRingLabelPad = 2 * kLabelPad;
constexpr int kAlarmBarH = 3;
constexpr int kHeadingScale = 2;
constexpr int kHeadingCy = kAlarmBarH + kRingLabelPad + kGlyphH;
constexpr int kHeadingLabelBottom = kHeadingCy + (kGlyphH * kHeadingScale) / 2 + kRingLabelPad;
constexpr int kFooterCy = kFar + kRingEdge;
constexpr int kFooterY = kFooterCy - kGlyphH / 2;
constexpr int kRangeLabelTop = kFooterCy - kGlyphH / 2 - kRingLabelPad;
constexpr int kRingLabelHalfW = (3 * kCellW * kHeadingScale - kHeadingScale) / 2 + kRingLabelPad;
constexpr int kCardinalOuterR = kOuterR - kGlyphH;
constexpr int kCardinalInnerR = kFar - kHeadingLabelBottom - kLabelPad - kGlyphH / 2;
constexpr int kCountScale = kHeadingScale;
constexpr int kCountY = kFooterY + kGlyphH - kGlyphH * kCountScale;
constexpr int kCountGap = 3;
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

void clear_behind(Framebuffer& fb, int x, int y, int w, int h, int pad) {
    fb.rect(x - pad, y - pad, w + 2 * pad, h + 2 * pad, false, true);
}

void ring_label(Framebuffer& fb, int cy, const char* s, int scale) {
    const int w = text_width(s, scale), h = kGlyphH * scale;
    clear_behind(fb, kCx - w / 2, cy - h / 2, w, h, kRingLabelPad);
    text_center(fb, kCx, cy - h / 2, s, scale);
}

const struct Cardinal {
    char letter;
    int32_t north, east;
} kCardinals[4] = {{'N', 1, 0}, {'E', 0, 1}, {'S', -1, 0}, {'W', 0, -1}};

bool clears_ring_labels(int cx, int cy) {
    if (cx + kGlyphW / 2 + kLabelPad < kCx - kRingLabelHalfW ||
        cx - kGlyphW / 2 - kLabelPad > kCx + kRingLabelHalfW)
        return true;
    return cy - kGlyphH / 2 > kHeadingLabelBottom && cy + kGlyphH / 2 < kRangeLabelTop;
}

HeadingUp cardinal_place(const Cardinal& p, int16_t track) {
    for (int r = kCardinalOuterR; r > kCardinalInnerR; r--) {
        const HeadingUp at = heading_up(p.north * r, p.east * r, track);
        if (clears_ring_labels(px_of(at.right), py_of(at.ahead))) return at;
    }
    return heading_up(p.north * kCardinalInnerR, p.east * kCardinalInnerR, track);
}

void cardinals(Framebuffer& fb, int16_t track) {
    for (const auto& p : kCardinals) {
        const HeadingUp at = cardinal_place(p, track);
        const int x = px_of(at.right) - kGlyphW / 2, y = py_of(at.ahead) - kGlyphH / 2;
        clear_behind(fb, x, y, kGlyphW, kGlyphH, kLabelPad);
        fb.draw_char(x, y, p.letter, true, 1);
    }
}

void heading_label(Framebuffer& fb, const RadarSnapshot& snap) {
    char buf[8];
    const int n = snap.have_fix ? fmt_uint(buf, snap.track_deg % 360, 3) : fmt_string(buf, "---");
    buf[n] = 0;
    ring_label(fb, kHeadingCy, buf, kHeadingScale);
}

void range_label(Framebuffer& fb, int32_t range_nm) {
    char buf[12];
    int n = fmt_uint(buf, static_cast<uint32_t>(range_nm));
    n += fmt_string(buf + n, " NM");
    buf[n] = 0;
    ring_label(fb, kFooterCy, buf, 1);
}

void satellites(Framebuffer& fb, uint8_t sats, bool have_fix) {
    char buf[4];
    buf[fmt_uint(buf, sats)] = 0;
    fb.draw_text(kMarginX, kCountY, buf, true, kCountScale);
    fb.draw_text(kMarginX + text_width(buf, kCountScale) + kCountGap, kFooterY,
                 have_fix ? "SAT" : "NO FIX", true, 1);
}

void aircraft(Framebuffer& fb, int in_view) {
    char buf[4];
    buf[fmt_uint(buf, static_cast<uint32_t>(in_view))] = 0;
    const int x = Framebuffer::kW - kMarginX - text_width(buf, kCountScale);
    fb.draw_text(x, kCountY, buf, true, kCountScale);
    fb.draw_text(x - kCountGap - text_width("ACT", 1), kFooterY, "ACT", true, 1);
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
    ring(fb, kOuterR / 2);

    const int16_t track = c16(snap.track_deg);
    if (snap.have_fix) cardinals(fb, track);

    draw_skyship(fb, kFar, kNear);

    const int in_view = snap.have_fix ? plot(fb, snap, track) : 0;

    heading_label(fb, snap);
    range_label(fb, snap.range_nm);
    satellites(fb, snap.sats, snap.have_fix);
    aircraft(fb, in_view);

    if (snap.max_alarm >= 3) fb.rect(0, 0, Framebuffer::kW, kAlarmBarH, true, true);
}

}  // namespace skyblip::ui
