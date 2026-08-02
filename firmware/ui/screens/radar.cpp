#include "ui/screens/radar.h"

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

}  // namespace

void draw_radar(Framebuffer& fb, const RadarSnapshot& snap) {
    fb.clear(true);

    ring(fb, kOuterR);
    ring(fb, kOuterR / 2);

    draw_skyship(fb, kFar, kNear);

    fb.draw_text(2, 2, snap.coverage ? "U" : "-", true, 1);

    if (!snap.have_fix) {
        fb.draw_text(kCx - 18, Framebuffer::kH - 12, "NO FIX", true, 1);
        return;
    }

    for (int i = 0; i < snap.n_targets; i++) {
        const RadarTarget& t = snap.targets[i];
        int64_t range = snap.range_m > 0 ? snap.range_m : 1;
        // Offsets are measured from the centre POINT, so east of it starts at
        // kFar and west of it at kNear - no half-pixel bias either way.
        int dx = static_cast<int>((static_cast<int64_t>(t.east_m) * kOuterR) / range);
        int dy = static_cast<int>((static_cast<int64_t>(t.north_m) * kOuterR) / range);
        int px = dx >= 0 ? kFar + dx : kNear + dx + 1;
        int py = dy <= 0 ? kFar - dy : kNear - dy + 1;
        if (dx * dx + dy * dy > kOuterR * kOuterR) {
            continue;
        }
        int r = t.alarm_level >= 2 ? 4 : 2;
        fb.circle(px, py, r, true, t.alarm_level >= 3);
        if (t.up_m > 30)
            fb.vline(px, py - r - 3, 2, true);
        else if (t.up_m < -30)
            fb.vline(px, py + r + 1, 2, true);
    }

    if (snap.max_alarm >= 3) fb.rect(0, 0, Framebuffer::kW, 3, true, true);
}

}  // namespace skyblip::ui
