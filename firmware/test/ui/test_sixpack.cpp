// The panel a pilot already knows how to read, so these check it against that
// habit rather than against pixels: the altimeter as a three-pointer, the card as
// a compass, the attitude dial banking with the turn and pitching with climb. Each
// needle has to follow its own datum. Two dials driven by the same value is a
// panel that looks right and lies.
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
    s.units = skyblip::settings::Units::Imperial;
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
    a.units = skyblip::settings::Units::Imperial;
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
    s.units = skyblip::settings::Units::Imperial;
    s.alt_ft = 2500;  // long hand at 500 ft (down), short hand at 2.5/10 (right)
    Framebuffer fb;
    draw_sixpack(fb, s);

    const Tile alt = kTiles[2];
    CHECK(fb.get_pixel(alt.cx, alt.cy + 20));        // hundreds hand, straight down
    CHECK(fb.get_pixel(alt.cx + 12, alt.cy));        // thousands hand, quarter turn
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

// B4. settings::units, on the page where it can mean something: the status page
// prints both columns, but a dial has one needle and one number under it, so a
// European glider pilot reading km/h and metres has to be able to ask for them.
TEST_CASE("sixpack: the unit setting decides the number under each dial, not just its label") {
    SixPackSnapshot imperial;
    imperial.have_data = true;
    imperial.units = skyblip::settings::Units::Imperial;
    imperial.speed_kt = 90;  // 166 km/h
    imperial.alt_ft = 3450;  // 1051 m
    imperial.vs_fpm = 500;   // 2.5 m/s
    SixPackSnapshot metric = imperial;
    metric.units = skyblip::settings::Units::Metric;

    Framebuffer fi, fm;
    draw_sixpack(fi, imperial);
    draw_sixpack(fm, metric);

    // The number under a dial is the converted one, drawn where the page draws
    // it: 90 kt reads 166, and it is not the same ink as 90.
    auto value_matches = [](const Framebuffer& fb, Tile t, const char* text) {
        const int value_y = t.cy + 33;
        Framebuffer expected;
        expected.clear(true);
        int n = 0;
        while (text[n]) n++;
        expected.draw_text(t.cx - (n * 6) / 2, value_y, text, true, 1);
        for (int y = value_y; y < value_y + 7; y++)
            for (int x = t.cx - 24; x <= t.cx + 24; x++)
                if (fb.get_pixel(x, y) != expected.get_pixel(x, y)) return false;
        return true;
    };
    CHECK(value_matches(fi, kTiles[0], "90"));
    CHECK(value_matches(fm, kTiles[0], "166"));
    CHECK(value_matches(fi, kTiles[2], "3450"));
    CHECK(value_matches(fm, kTiles[2], "1051"));
    // A vario is read to the decimal: 500 fpm is +2.5 m/s, not +2.
    CHECK(value_matches(fi, kTiles[5], "+500"));
    CHECK(value_matches(fm, kTiles[5], "+2.5"));

    // The altimeter stays a three-pointer graduated 100 to the mark, so the two
    // faces cannot look alike either: at 3450 ft the long hand sits at 450, at
    // 1051 m it sits at 51.
    CHECK(black_in(fm, kTiles[2], 30) != black_in(fi, kTiles[2], 30));
}

TEST_CASE("sixpack: the attitude dial banks with the turn and pitches with climb") {
    SixPackSnapshot level;
    level.have_data = true;
    level.units = skyblip::settings::Units::Imperial;
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
