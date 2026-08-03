// Rate of turn from two reported tracks. Own-ship differentiates its own GNSS
// track and the alarm differentiates a target's, so this arithmetic decides
// both what the six-pack's needle shows and whether two gliders are judged to
// be circling together. Two things can go wrong and both are pinned here: the
// difference must be taken the short way round, so a heading crossing north is
// a couple of degrees and not most of a circle; and cordic9 is 512 units to the
// turn, not 360, so the scale is 45/64 of a degree per unit.
#include "core/flight/turn.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::flight;

namespace {

// cordic9 from whole degrees, the way a fix and an ADS-L frame carry a track.
uint16_t c9(int deg) { return static_cast<uint16_t>(((deg % 360 + 360) % 360) * 512 / 360); }

}  // namespace

TEST_CASE("turn: the difference between two tracks is taken the short way round") {
    CHECK(track_delta_c9(c9(10), c9(350)) > 0);
    CHECK(track_delta_c9(c9(350), c9(10)) < 0);
    // 20 degrees is 28 cordic9 units, and the wrap must not make it 484.
    CHECK(track_delta_c9(c9(10), c9(350)) < 32);
    CHECK(track_delta_c9(c9(350), c9(10)) > -32);
    // The same track is no turn at all, whichever side of the wrap it sits on.
    CHECK(track_delta_c9(c9(0), c9(0)) == 0);
    CHECK(track_delta_c9(c9(359), c9(359)) == 0);
}

TEST_CASE("turn: a half turn is signed, never larger than half a circle") {
    for (int deg = 0; deg < 360; deg += 5) {
        const int16_t d = track_delta_c9(c9(deg + 180), c9(deg));
        CAPTURE(deg);
        CHECK(d >= -256);
        CHECK(d <= 256);
    }
}

// A glider thermalling turns at 14 to 18 deg/s and the alarm decides on those
// numbers whether two aircraft are circling together, so the scale has to be
// right and not merely consistent. One cordic9 unit is 0.7 deg and the division
// truncates, so a rate read off a differentiated track is worth a degree per
// second, which is the tolerance below and not a rounding excuse.
TEST_CASE("turn: the rate is degrees per second, positive to the right") {
    CHECK(turn_rate_dps(c9(90), c9(0), 1000) == 90);
    CHECK(turn_rate_dps(c9(0), c9(90), 1000) == -90);

    // A full circle in 24 s, read over one second of it and over two.
    CHECK(turn_rate_dps(c9(105), c9(90), 1000) >= 14);
    CHECK(turn_rate_dps(c9(105), c9(90), 1000) <= 15);
    CHECK(turn_rate_dps(c9(120), c9(90), 2000) >= 14);
    CHECK(turn_rate_dps(c9(120), c9(90), 2000) <= 15);

    // And through north, which is where the sign used to invert.
    CHECK(turn_rate_dps(c9(5), c9(350), 1000) >= 14);
    CHECK(turn_rate_dps(c9(5), c9(350), 1000) <= 16);
    CHECK(turn_rate_dps(c9(350), c9(5), 1000) <= -14);
}

TEST_CASE("turn: straight and level is zero, and no interval is not a turn") {
    CHECK(turn_rate_dps(c9(90), c9(90), 1000) == 0);
    CHECK(turn_rate_dps(c9(90), c9(0), 0) == 0);
}
