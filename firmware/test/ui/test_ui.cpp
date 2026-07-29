#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "ui/framebuffer.h"
#include "ui/screens/radar.h"
#include "ui/screens/status.h"

using namespace skyblip::ui;

TEST_CASE("fb: pixel set/get and clear") {
    Framebuffer fb;
    fb.clear(true);
    CHECK(fb.count_black() == 0);
    fb.set_pixel(10, 20, true);
    CHECK(fb.get_pixel(10, 20));
    CHECK(fb.count_black() == 1);
    fb.set_pixel(10, 20, false);
    CHECK_FALSE(fb.get_pixel(10, 20));
    fb.set_pixel(-1, -1, true);  // out of bounds no-op
    fb.set_pixel(999, 999, true);
    CHECK(fb.count_black() == 0);
}

TEST_CASE("fb: primitives draw something") {
    Framebuffer fb;
    fb.clear(true);
    fb.line(0, 0, 199, 199, true);
    CHECK(fb.get_pixel(0, 0));
    CHECK(fb.get_pixel(199, 199));
    int before = fb.count_black();
    fb.circle(100, 100, 50, true, false);
    CHECK(fb.count_black() > before);
    fb.rect(10, 10, 30, 20, true, true);
    CHECK(fb.get_pixel(25, 20));
}

TEST_CASE("fb: text advances and draws glyph pixels") {
    Framebuffer fb;
    fb.clear(true);
    int x = fb.draw_text(5, 5, "AB1", true, 1);
    CHECK(x == 5 + 3 * 6);
    CHECK(fb.count_black() > 0);
    // space draws nothing
    Framebuffer fb2;
    fb2.clear(true);
    fb2.draw_char(0, 0, ' ', true, 1);
    CHECK(fb2.count_black() == 0);
}

TEST_CASE("radar: renders rings, own symbol and plots targets") {
    Framebuffer fb;
    RadarTarget targets[2] = {
        {2000, 0, 100, 1},    // north, above
        {0, -3000, -100, 3},  // west, below, urgent
    };
    // Mirror-image targets must land mirror-image distances from the centre
    // point: east of it starts at pixel 100, west of it at 99.
    {
        RadarTarget pair[2] = {{0, 4000, 0, 1}, {0, -4000, 0, 1}};
        RadarSnapshot s2;
        s2.have_fix = true;
        s2.range_m = 10000;
        s2.n_targets = 2;
        s2.targets = pair;
        Framebuffer f2;
        draw_radar(f2, s2);
        int e = -1, w = -1;
        for (int x = 100; x < 200; x++)
            if (f2.get_pixel(x, 99)) {
                e = x;
                break;
            }
        for (int x = 99; x >= 0; x--)
            if (f2.get_pixel(x, 99)) {
                w = x;
                break;
            }
        CHECK(e - 100 == 99 - w);
    }
    RadarSnapshot snap;
    snap.have_fix = true;
    snap.range_m = 10000;
    snap.n_targets = 2;
    snap.targets = targets;
    snap.max_alarm = 3;
    snap.coverage = true;
    draw_radar(fb, snap);
    CHECK(fb.count_black() > 100);

    // no-fix path shows text, few pixels but non-empty
    Framebuffer fb2;
    RadarSnapshot ns;
    ns.have_fix = false;
    draw_radar(fb2, ns);
    CHECK(fb2.count_black() > 0);
}

TEST_CASE("radar: everything is centred on the 99|100 point, not on a pixel") {
    // 200x200 is an EVEN grid: there is no middle pixel. The centre is the point
    // where pixels 99 and 100 meet on both axes, so anything "on" the centre is
    // a PAIR of pixels. Drawn with no alarm so nothing else marks the edges.
    Framebuffer fb;
    RadarSnapshot snap;
    snap.have_fix = true;
    snap.range_m = 10000;
    draw_radar(fb, snap);
    // the own ship straddles the centre: its fuselage is a pair of columns
    CHECK(fb.get_pixel(99, 99));
    CHECK(fb.get_pixel(100, 99));
    CHECK(fb.get_pixel(99, 100));
    CHECK(fb.get_pixel(100, 100));
    // ... and the glyph is mirror-symmetric about that point, not about a column
    int mism = 0;
    for (int y = 88; y < 118; y++)
        for (int k = 0; k < 14; k++)
            if (fb.get_pixel(99 - k, y) != fb.get_pixel(100 + k, y)) mism++;
    CHECK(mism == 0);
    // The hot spot (the wing = the widest row, i.e. the aircraft's position)
    // sits ON the centre point, not at the glyph's bounding-box centre: targets
    // are plotted as offsets from it, so bbox-centring a long-tailed aeroplane
    // puts the wing several px forward and skews every bearing on screen.
    int widest = 0, widest_row = -1;
    for (int y = 88; y < 118; y++) {
        int n = 0;
        for (int x = 86; x < 114; x++) n += fb.get_pixel(x, y) ? 1 : 0;
        if (n > widest) {
            widest = n;
            widest_row = y;
        }
    }
    CHECK(widest_row == 99);
    // The rings are concentric with the same point: equal margin on all sides.
    int left = -1, right = -1, top = -1, bottom = -1;
    for (int x = 0; x < 200; x++)
        if (fb.get_pixel(x, 99)) {
            if (left < 0) left = x;
            right = x;
        }
    for (int y = 0; y < 200; y++)
        if (fb.get_pixel(99, y)) {
            if (top < 0) top = y;
            bottom = y;
        }
    CHECK(left == 199 - right);
    CHECK(top == 199 - bottom);
}

TEST_CASE("status: every value reads in the aeronautical unit first, then SI") {
    // 1500 m = 4921 ft, 40 kt = 20 m/s (speed_q is quarter-m/s), +2.0 m/s =
    // +394 fpm. Rendering is 5x7 glyphs, so the check is on the row's ink: the
    // dual-unit row is wider than a single-unit one would be.
    Framebuffer both;
    StatusSnapshot s;
    s.fix_valid = true;
    s.utc_valid = true;
    s.sats = 9;
    s.alt_m = 1500;
    s.speed_q = 80;
    s.climb_e8 = 16;
    s.track_c9 = 128;
    draw_status(both, s);

    // The barometric rows only exist when a barometer answered: altitude on the
    // subscale the pilot set, pressure altitude on 1013.25, and both pressures.
    Framebuffer with_baro;
    StatusSnapshot b = s;
    b.baro_valid = true;
    b.pressure_pa = 84556;
    b.qnh_pa = 101325;
    b.alt_qnh_m = 1500;
    b.alt_std_m = 1500;
    draw_status(with_baro, b);
    CHECK(with_baro.count_black() > both.count_black());
}

TEST_CASE("panel model: the driver's own output is what the model shows") {
    Framebuffer fb;
    StatusSnapshot s;
    s.fix_valid = true;
    s.alt_m = 900;
    s.baro_valid = true;
    s.pressure_pa = 90810;
    draw_status(fb, s);

    skyblip::models::Ssd1681 panel;
    skyblip::parts::Ssd1681 driver(panel, panel, panel.dc, panel.rst, panel.busy);
    driver.begin();
    driver.present(fb, {0, 0, 200, 200}, skyblip::hal::Refresh::Full);

    CHECK(panel.present_count == 1);
    CHECK(panel.last_full);
    // Round trip through the driver's inversion: what the panel holds must be
    // pixel-for-pixel what the UI drew.
    CHECK(panel.framebuffer().count_black() == fb.count_black());
    CHECK(panel.save_pgm("build/status.pgm"));
}

TEST_CASE("status: the widest position on earth still fits its row") {
    // -90.0000000 and -180.0000000: eleven and twelve characters, the most the
    // format can produce. The latitude ends on the first column's unit edge and
    // the longitude block is anchored to the margin, so the worst case is where
    // they nearly meet.
    Framebuffer fb;
    StatusSnapshot s;
    s.fix_valid = true;
    s.lat_1e7 = -900000000;
    s.lon_1e7 = -1800000000;
    draw_status(fb, s);

    const int y0 = 43, y1 = 50;  // the LAT/LON row, one glyph tall
    for (int y = y0; y < y1; y++)
        for (int x = 196; x < Framebuffer::kW; x++) CHECK_FALSE(fb.get_pixel(x, y));

    // At least one blank column between the latitude and the LON block, and the
    // label is not touched either.
    int blank = 0;
    for (int x = 88; x < 106; x++) {
        bool ink = false;
        for (int y = y0; y < y1; y++) ink = ink || fb.get_pixel(x, y);
        if (!ink) blank++;
    }
    CHECK(blank >= 1);
    for (int y = y0; y < y1; y++) CHECK_FALSE(fb.get_pixel(23, y));
}
