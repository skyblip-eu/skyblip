#include "ui/screens/radar.h"

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
// side and kFar+(d-1) on the other; a feature ON the centre is a PAIR of
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

// skyShip - skyBlip's ownship symbol (RTCA DO-257A's term for the "this is you"
// mark): a low-wing single in plan, nose-up, hinted by hand for a 1-bit raster
// in skyship-editor/ and kept there as skyship.txt.
//
//   - 24 x 16 px. EVEN width, so the sprite straddles the screen's centre point
//     (200x200 has no middle pixel - the centre is where 99 and 100 meet).
//   - the wing's TWO full-span rows are the hot spot, and with kGlyphOriginRow
//     = 5 they land on 99 and 100 - so the aircraft's position straddles the
//     centre point vertically as well as laterally. Nothing is half a pixel off.
//   - the propeller, fuselage and tailplane are deliberately oversize relative
//     to a real airframe: below about 2 px a feature stops reading at all, so at
//     24 px of span overall proportion is traded for legibility of the details.
//   - the leading edge is raked back at the tip. A straight leading edge reads as
//     a rounded rectangle at this size rather than as a wing.
//
// This is the DEFAULT symbol, compiled in. The plan is for a user to load their
// own into QSPI flash and pick one per aircraft, at which point this becomes the
// fallback and the sprite is read from storage - which is why Ownship below is a
// value carrying its own size and hot spot, not three loose constants.
//
// Edit:    open skyship-editor/index.html          (draw, then copy the art)
// Apply:   python3 skyship-editor/apply.py --origin 5 skyship-editor/skyship.txt
// Retrace: python3 skyship-editor/trace.py 30 --plan <plan-view>.png
//
// One bit per pixel, LSB = leftmost column; the art beside each word is the
// table. kGlyphOriginRow is the HOT SPOT: the row that lands on the plot origin,
// i.e. where the aircraft actually is. It is NOT the bounding-box centre -
// bbox-centring this shape dragged the wing 4 px forward, drawing a target
// 20 px abeam 11 degrees off its true bearing (and 4% off in range: 435 m at
// the 10 km setting). DO-257A's guidance is 1% of the depiction, so that was
// twice the accepted figure.
constexpr int kGlyphW = 24;
constexpr int kGlyphOriginRow = 5;
constexpr uint32_t kOwnshipRows[] = {
    0x00001800,  // ...........##...........
    0x0000ff00,  // ........########........
    0x00003c00,  // ..........####..........
    0x00003c00,  // ..........####..........
    0x003ffffc,  // ..####################..
    0x00ffffff,  // ########################
    0x00ffffff,  // ########################
    0x003ffffc,  // ..####################..
    0x0007ffe0,  // .....##############.....
    0x00003c00,  // ..........####..........
    0x00001800,  // ...........##...........
    0x00001800,  // ...........##...........
    0x0000ff00,  // ........########........
    0x0001ff80,  // .......##########.......
    0x0000ff00,  // ........########........
    0x00001800,  // ...........##...........
};
constexpr int kGlyphH = static_cast<int>(sizeof(kOwnshipRows) / sizeof(kOwnshipRows[0]));

// A 1-bit ownship sprite: the bitmap plus the two things a renderer cannot
// guess - how wide a row is, and which row is the aircraft's position.
struct Ownship {
    int w;  // EVEN, so the sprite straddles the centre point
    int h;
    int origin_row;        // hot spot: this row lands on the centre point
    const uint32_t* rows;  // one word per row, LSB = leftmost column
};
constexpr Ownship kDefaultOwnship{kGlyphW, kGlyphH, kGlyphOriginRow, kOwnshipRows};

void draw_ownship(Framebuffer& fb, const Ownship& s) {
    // s.w is EVEN, so the fuselage straddles kNear|kFar and the sprite is
    // exactly centred; the hot-spot row is laid on kNear, which puts the
    // aircraft's position on the centre point itself, not half a pixel off it.
    int x0 = kFar - s.w / 2, y0 = kNear - s.origin_row;
    static_assert(kGlyphW % 2 == 0, "even width: the centre is a pixel PAIR");
    for (int row = 0; row < s.h; row++) {
        uint32_t bits = s.rows[row];
        for (int col = 0; bits; col++, bits >>= 1) {
            if (bits & 1) fb.set_pixel(x0 + col, y0 + row, true);
        }
    }
}
}  // namespace

void draw_radar(Framebuffer& fb, const RadarSnapshot& snap) {
    fb.clear(true);

    ring(fb, kOuterR);
    ring(fb, kOuterR / 2);

    draw_ownship(fb, kDefaultOwnship);

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
