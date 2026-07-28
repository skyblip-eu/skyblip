#include "doctest/doctest.h"
#include "ui/framebuffer.h"
#include "ui/screens/sixpack.h"

using namespace skyblip::ui;

namespace {
// The six dial centres, in the order the panel draws them.
struct Tile {
    int cx, cy;
};
const Tile kTiles[6] = {{34, 56}, {100, 56}, {166, 56}, {34, 138}, {100, 138}, {166, 138}};

int black_in(const Framebuffer& fb, Tile t, int r) {
    int n = 0;
    for (int y = t.cy - r; y <= t.cy + r; y++)
        for (int x = t.cx - r; x <= t.cx + r; x++)
            if (fb.get_pixel(x, y)) n++;
    return n;
}
}  // namespace

TEST_CASE("sixpack: six dials are drawn, each with its own needle") {
    SixPackSnapshot s;
    s.have_data = true;
    s.speed_kt = 90;
    s.alt_ft = 3450;
    s.vs_fpm = 500;
    s.track_deg = 270;
    s.turn_dps = 3;

    Framebuffer fb;
    draw_sixpack(fb, s);
    for (const Tile& t : kTiles) CHECK(black_in(fb, t, 30) > 60);

    // Without a fix the dials stay, the needles go: every tile must lose ink.
    SixPackSnapshot none;
    Framebuffer empty;
    draw_sixpack(empty, none);
    for (const Tile& t : kTiles) CHECK(black_in(empty, t, 30) < black_in(fb, t, 30));
}

TEST_CASE("sixpack: needles move with the data they show") {
    SixPackSnapshot a;
    a.have_data = true;
    a.speed_kt = 40;
    a.alt_ft = 1200;
    a.track_deg = 0;
    SixPackSnapshot b = a;
    b.speed_kt = 140;
    b.alt_ft = 1900;
    b.track_deg = 180;

    Framebuffer fa, fbuf;
    draw_sixpack(fa, a);
    draw_sixpack(fbuf, b);

    bool differs[6] = {};
    for (int i = 0; i < 6; i++) {
        for (int y = kTiles[i].cy - 28; y <= kTiles[i].cy + 28 && !differs[i]; y++)
            for (int x = kTiles[i].cx - 28; x <= kTiles[i].cx + 28; x++)
                if (fa.get_pixel(x, y) != fbuf.get_pixel(x, y)) {
                    differs[i] = true;
                    break;
                }
    }
    CHECK(differs[0]);  // airspeed
    CHECK(differs[2]);  // altimeter
    CHECK(differs[4]);  // heading
}

TEST_CASE("sixpack: the altimeter reads like a three-pointer, the card like a compass") {
    SixPackSnapshot s;
    s.have_data = true;
    s.alt_ft = 2500;  // long hand at 500 ft (down), short hand at 2.5/10 (right)
    Framebuffer fb;
    draw_sixpack(fb, s);

    const Tile alt = kTiles[2];
    CHECK(fb.get_pixel(alt.cx, alt.cy + 20));       // hundreds hand, straight down
    CHECK(fb.get_pixel(alt.cx + 12, alt.cy));       // thousands hand, quarter turn
    CHECK_FALSE(fb.get_pixel(alt.cx + 20, alt.cy));  // and it is the SHORT one
    CHECK_FALSE(fb.get_pixel(alt.cx, alt.cy - 20));

    // The compass card turns with the track: north swings to the right when the
    // aircraft flies west.
    SixPackSnapshot west = s;
    west.track_deg = 270;
    Framebuffer fw;
    draw_sixpack(fw, west);
    const Tile hdg = kTiles[4];
    int right = 0, left = 0;
    for (int y = hdg.cy - 8; y <= hdg.cy + 8; y++)
        for (int d = 12; d <= 24; d++) {
            if (fw.get_pixel(hdg.cx + d, y)) right++;
            if (fw.get_pixel(hdg.cx - d, y)) left++;
        }
    CHECK(right > left);
}

TEST_CASE("sixpack: the attitude dial banks with the turn and pitches with climb") {
    SixPackSnapshot level;
    level.have_data = true;
    level.speed_kt = 100;
    SixPackSnapshot turning = level;
    turning.turn_dps = 3;  // standard rate at 100 kt is ~15 deg of bank
    SixPackSnapshot climbing = level;
    climbing.vs_fpm = 1000;

    Framebuffer f0, f1, f2;
    draw_sixpack(f0, level);
    draw_sixpack(f1, turning);
    draw_sixpack(f2, climbing);

    const Tile att = kTiles[1];
    CHECK(black_in(f1, att, 28) != black_in(f0, att, 28));
    // Climbing shows more sky: the ground area shrinks.
    CHECK(black_in(f2, att, 28) < black_in(f0, att, 28));
}
