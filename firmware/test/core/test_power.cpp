#include "core/power/battery.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::power;

static messages::BatterySample sample(uint16_t millivolts, bool external_power = false) {
    return messages::BatterySample{millivolts, external_power};
}

// The gauge medians three readings, so a settled value takes three - which is the
// same reason a single transmit burst cannot move the needle.
static void settle(Gauge& gauge, uint16_t millivolts, bool external_power = false) {
    for (int i = 0; i < 4; i++) gauge.apply(sample(millivolts, external_power));
}

TEST_CASE("battery: the curve is monotonic and clamps at both ends") {
    CHECK(percent_from_mv(3000, false) == 0);
    CHECK(percent_from_mv(kEmptyMv, false) == 0);
    CHECK(percent_from_mv(kFullMv, false) == 100);
    CHECK(percent_from_mv(4500, false) == 100);

    uint8_t previous = 0;
    for (uint16_t mv = kEmptyMv; mv <= kFullMv; mv += 10) {
        const uint8_t percent = percent_from_mv(mv, false);
        CHECK(percent >= previous);
        previous = percent;
    }
}

// The point of the whole module: the same reading means two different states of
// charge, because on charge the terminal sits above the cell's own voltage.
TEST_CASE("battery: charging reads lower than resting at the same voltage") {
    for (uint16_t mv = 3600; mv <= 4150; mv += 50)
        CHECK(percent_from_mv(mv, true) < percent_from_mv(mv, false));

    // 4.00 V is nearly full on the bench and barely half full with a charger on it.
    CHECK(percent_from_mv(4000, false) == 89);
    CHECK(percent_from_mv(4000, true) == 55);
}

TEST_CASE("battery: the flat middle of the cell is not a straight line") {
    // 3.70 -> 3.80 V is a quarter of the capacity; 4.10 -> 4.20 V is a twentieth.
    const int mid = percent_from_mv(3800, false) - percent_from_mv(3700, false);
    const int top = percent_from_mv(4200, false) - percent_from_mv(4100, false);
    CHECK(mid > top);
}

TEST_CASE("gauge: nothing is reported before the first reading") {
    Gauge gauge;
    CHECK_FALSE(gauge.state().valid);
    CHECK(gauge.state().millivolts == 0);

    gauge.apply(sample(3900));
    CHECK(gauge.state().valid);
    // The first reading stands on its own rather than being mixed with zeroes.
    CHECK(gauge.state().millivolts == 3900);
    CHECK(gauge.state().percent == percent_from_mv(3900, false));
    CHECK_FALSE(gauge.state().charging);
}

TEST_CASE("gauge: one transmit burst does not move the gauge") {
    Gauge gauge;
    settle(gauge, 3900);
    const uint8_t before = gauge.state().percent;

    // A 22 dBm burst sags the rail for a single reading. The median throws it out
    // whole: 200 mV of transient must not read as a fifth of the pack gone.
    gauge.apply(sample(3700));
    CHECK(gauge.state().millivolts == 3900);
    CHECK(gauge.state().percent == before);

    // Two readings in a row are the cell, not a burst.
    gauge.apply(sample(3700));
    CHECK(gauge.state().millivolts == 3700);
    CHECK(gauge.state().percent < before);
}

TEST_CASE("gauge: the percentage only moves the way the current flows") {
    Gauge gauge;
    settle(gauge, 3800);
    const uint8_t discharging = gauge.state().percent;

    // Noise upwards while running on the cell is noise, not charge: two readings
    // to get past the median, and the percentage still does not climb.
    gauge.apply(sample(3830));
    gauge.apply(sample(3830));
    CHECK(gauge.state().millivolts == 3830);
    CHECK(gauge.state().percent == discharging);

    // Draining further does move it.
    settle(gauge, 3700);
    CHECK(gauge.state().percent < discharging);
}

TEST_CASE("gauge: plugging in re-seats on the charge curve, unplugging on the other") {
    Gauge gauge;
    settle(gauge, 4000);
    const uint8_t resting = gauge.state().percent;
    CHECK(resting == percent_from_mv(4000, false));

    // Same cell, cable in: the charger is pushing, so the honest number drops.
    settle(gauge, 4000, /*external_power=*/true);
    CHECK(gauge.state().charging);
    CHECK(gauge.state().external_power);
    CHECK(gauge.state().percent == percent_from_mv(4000, true));
    CHECK(gauge.state().percent < resting);

    // Cable out: the charge current stops, the cell relaxes, and the gauge is
    // allowed to jump back up rather than being held down by the old curve.
    settle(gauge, 4000, /*external_power=*/false);
    CHECK_FALSE(gauge.state().charging);
    CHECK(gauge.state().percent == resting);
}

TEST_CASE("gauge: charging never walks backwards") {
    Gauge gauge;
    settle(gauge, 3900, /*external_power=*/true);
    const uint8_t climbing = gauge.state().percent;

    gauge.apply(sample(3870, /*external_power=*/true));
    CHECK(gauge.state().percent == climbing);

    settle(gauge, 4100, /*external_power=*/true);
    CHECK(gauge.state().percent > climbing);
}

TEST_CASE("gauge: a full cell on the cable is charged, not charging") {
    Gauge gauge;
    settle(gauge, kFullMv, /*external_power=*/true);
    CHECK(gauge.state().external_power);
    // No charge current can be measured, so the float plateau is the signal that
    // the charger has finished.
    CHECK(gauge.state().millivolts >= kChargeCompleteMv);
    CHECK_FALSE(gauge.state().charging);
    CHECK(gauge.state().percent == 100);
}
