#include "ui/widgets/skyship.h"

namespace skyblip::ui {

namespace {
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

}  // namespace

// s.w is EVEN, so the fuselage straddles the pixel pair (cx-1|cx) and the sprite
// is exactly centred; the hot-spot row is laid on cy, which puts the aircraft's
// position on the plot origin itself, not half a pixel off it.
void draw_skyship(Framebuffer& fb, int cx, int cy) {
    const Ownship& s = kDefaultOwnship;
    static_assert(kGlyphW % 2 == 0, "even width: the centre is a pixel PAIR");
    const int x0 = cx - s.w / 2, y0 = cy - s.origin_row;
    for (int row = 0; row < s.h; row++) {
        uint32_t bits = s.rows[row];
        for (int col = 0; bits; col++, bits >>= 1) {
            if (bits & 1) fb.set_pixel(x0 + col, y0 + row, true);
        }
    }
}

}  // namespace skyblip::ui
