// One terminal voltage, two meanings: on the cable the charger holds the cell
// above its own resting voltage, so the same reading is a far emptier cell. These
// pin down that the gauge says which curve it read, never walks the wrong way, and
// ignores the sag of a 22 dBm burst. A percentage that jumps when the radio keys
// is a gauge a pilot stops believing.
#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"
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

// The acting half. The gauge above says what the cell holds; this says when that
// number stops being a reading and becomes a decision. SoftRF's shape, because
// it is the one that survived contact with the same pack on the same board:
// 3.5 V warns, 3.2 V cuts off, three consecutive samples before either, and a
// 1.8 V floor under both so an unconnected divider cannot switch the device off
// in someone's hand.

TEST_CASE("cutoff: two samples below the cutoff are not enough, the third acts") {
    // Under the rule, two samples claim nothing at all: the monitor has not yet
    // separated a real cell from a burst sagging the rail.
    CutoffMonitor monitor;
    CHECK(monitor.apply(sample(3100)) == PowerLevel::Unknown);
    CHECK(monitor.apply(sample(3100)) == PowerLevel::Unknown);
    CHECK_FALSE(monitor.cutoff());
    CHECK(monitor.apply(sample(3100)) == PowerLevel::Cutoff);
    CHECK(monitor.cutoff());
}

TEST_CASE("cutoff: one good sample resets the count, so a transmit sag cannot act") {
    CutoffMonitor monitor;
    monitor.apply(sample(3100));
    monitor.apply(sample(3100));
    monitor.apply(sample(3800));  // the burst ended, the rail came back
    CHECK(monitor.below_cutoff() == 0);
    CHECK(monitor.level() == PowerLevel::Normal);

    monitor.apply(sample(3100));
    monitor.apply(sample(3100));
    CHECK_FALSE(monitor.cutoff());
}

TEST_CASE("cutoff: both thresholds are boundaries, not ranges") {
    // Exactly at the warning: still normal. One millivolt under: warned.
    CutoffMonitor at_warn;
    for (int i = 0; i < 4; i++) at_warn.apply(sample(kLowWarnMv));
    CHECK(at_warn.level() == PowerLevel::Normal);

    CutoffMonitor under_warn;
    for (int i = 0; i < 4; i++) under_warn.apply(sample(kLowWarnMv - 1));
    CHECK(under_warn.level() == PowerLevel::Low);

    // Exactly at the cutoff: warned, not cut off. One millivolt under: cut off.
    CutoffMonitor at_cutoff;
    for (int i = 0; i < 4; i++) at_cutoff.apply(sample(kCutoffMv));
    CHECK(at_cutoff.level() == PowerLevel::Low);
    CHECK_FALSE(at_cutoff.cutoff());

    CutoffMonitor under_cutoff;
    for (int i = 0; i < 4; i++) under_cutoff.apply(sample(kCutoffMv - 1));
    CHECK(under_cutoff.cutoff());
}

TEST_CASE("cutoff: the warning comes before the cutoff, never instead of it") {
    CutoffMonitor monitor;
    for (int i = 0; i < 3; i++) monitor.apply(sample(3400));
    CHECK(monitor.level() == PowerLevel::Low);
    CHECK(monitor.warned());
    CHECK_FALSE(monitor.cutoff());

    for (int i = 0; i < 3; i++) monitor.apply(sample(3100));
    CHECK(monitor.cutoff());
}

TEST_CASE("cutoff: a floating ADC cannot power the device off") {
    CutoffMonitor monitor;
    for (int i = 0; i < 20; i++) monitor.apply(sample(kImplausibleFloorMv));
    CHECK_FALSE(monitor.cutoff());
    CHECK(monitor.level() == PowerLevel::Unknown);
    CHECK(monitor.implausible() == 20);

    // And a divider that reads zero is the same case, not an empty cell.
    CutoffMonitor disconnected;
    for (int i = 0; i < 20; i++) disconnected.apply(sample(0));
    CHECK_FALSE(disconnected.cutoff());

    // One millivolt above the floor is a reading again, and it does act.
    CutoffMonitor believable;
    for (int i = 0; i < 3; i++) believable.apply(sample(kImplausibleFloorMv + 1));
    CHECK(believable.cutoff());
}

TEST_CASE("cutoff: nothing acts on a terminal the charger is holding up") {
    CutoffMonitor monitor;
    for (int i = 0; i < 10; i++) monitor.apply(sample(3100, /*external_power=*/true));
    CHECK(monitor.level() == PowerLevel::Normal);
    CHECK_FALSE(monitor.cutoff());
}

TEST_CASE("cutoff: the decision latches once it is made") {
    CutoffMonitor monitor;
    for (int i = 0; i < 3; i++) monitor.apply(sample(3100));
    REQUIRE(monitor.cutoff());
    // The radio going quiet lets the cell relax upwards. It is still empty.
    for (int i = 0; i < 10; i++) monitor.apply(sample(4000));
    CHECK(monitor.cutoff());
}

// Three things ask for the rails to drop and all three take the same road. The
// order matters more than the entry: the radio and the panel are parked first,
// and the wake pin is armed only once the button is actually up.

TEST_CASE("shutdown: a short press is not a shutdown, a long one is") {
    ShutdownSequencer seq;
    uint32_t t = 0;
    seq.tick(t, false);  // the button starts up: the hold is armed

    for (t = 10; t < 500; t += 10) seq.tick(t, true);
    seq.tick(t, false);
    CHECK(seq.phase() == ShutdownPhase::Running);
    CHECK(seq.reason() == ShutdownReason::None);

    for (t = 1000; t < 1000 + kLongPressMs + 100; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
    CHECK(seq.reason() == ShutdownReason::LongPress);
}

TEST_CASE("shutdown: the wake pin waits for the button to come up") {
    ShutdownSequencer seq;
    uint32_t t = 0;
    seq.tick(t, false);
    for (t = 10; t <= 10 + kLongPressMs; t += 10) seq.tick(t, true);
    REQUIRE(seq.phase() == ShutdownPhase::Parking);

    // The panel needs its full refresh before the rails may go.
    for (; t < 10 + kLongPressMs + kParkMs; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);

    // Parked, but the finger is still on the button: a level-sensed wake pin
    // armed now brings the device straight back up.
    for (; t < 30000; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::AwaitRelease);
    CHECK_FALSE(seq.ready_to_power_off());

    const uint32_t released = t;
    for (; t < released + kReleaseSettleMs; t += 10) seq.tick(t, false);
    CHECK_FALSE(seq.ready_to_power_off());
    seq.tick(t + kReleaseSettleMs, false);
    CHECK(seq.phase() == ShutdownPhase::Off);
    CHECK(seq.ready_to_power_off());
}

TEST_CASE("shutdown: a device woken by the button does not switch itself off again") {
    // SYSTEM OFF is left by a press, so the first thing the sequencer ever sees
    // is a button that is already down. Counting that as a hold powers the
    // device off before the panel has drawn a single frame.
    ShutdownSequencer seq;
    for (uint32_t t = 0; t < 10000; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Running);

    // Once it has been released, the next hold counts.
    uint32_t t = 10000;
    seq.tick(t, false);
    for (; t <= 10000 + kLongPressMs + 10; t += 10) seq.tick(t, true);
    CHECK(seq.phase() == ShutdownPhase::Parking);
}

TEST_CASE("shutdown: low battery and the link take the same road as the button") {
    for (const ShutdownReason reason : {ShutdownReason::LowBattery, ShutdownReason::LinkRequest}) {
        ShutdownSequencer seq;
        seq.request(reason, 0);
        CHECK(seq.phase() == ShutdownPhase::Parking);
        CHECK(seq.reason() == reason);
        CHECK(seq.going_down());

        uint32_t t = 0;
        for (; t < kParkMs + kReleaseSettleMs + 100; t += 10) seq.tick(t, false);
        CHECK(seq.ready_to_power_off());
    }
}

TEST_CASE("shutdown: nothing cancels a shutdown once it has started") {
    ShutdownSequencer seq;
    seq.request(ShutdownReason::LowBattery, 0);
    seq.request(ShutdownReason::LinkRequest, 10);
    CHECK(seq.reason() == ShutdownReason::LowBattery);

    for (uint32_t t = 0; t < 20000; t += 10) seq.tick(t, false);
    CHECK(seq.phase() == ShutdownPhase::Off);
    // Even a fresh press cannot bring it back.
    seq.tick(20000, true);
    CHECK(seq.phase() == ShutdownPhase::Off);
}

TEST_CASE("shutdown: the hold is readable while it fills up") {
    ShutdownSequencer seq;
    seq.tick(0, false);
    CHECK(seq.held_ms(0) == 0);
    seq.tick(100, true);
    seq.tick(600, true);
    CHECK(seq.held_ms(600) == 500);
    seq.tick(700, false);
    CHECK(seq.held_ms(700) == 0);
}

// Which reset the device came out of. Seven causes, one register, and the nRF52
// latches them: after a watchdog bite that was followed by a soft reset, both
// bits are set and only one of the two is the diagnosis.

TEST_CASE("reset: a fault cause outranks the benign one that came with it") {
    CHECK(classify(ResetCause::Watchdog | ResetCause::Software) == ResetReason::Watchdog);
    CHECK(classify(ResetCause::Lockup | ResetCause::Pin) == ResetReason::Lockup);
    CHECK(classify(ResetCause::Brownout | ResetCause::PowerOn) == ResetReason::Brownout);
    CHECK(classify(ResetCause::Watchdog | ResetCause::Lockup) == ResetReason::Watchdog);
}

TEST_CASE("reset: every cause the silicon can raise has one name") {
    CHECK(classify(ResetCause::None) == ResetReason::Unknown);
    CHECK(classify(ResetCause::PowerOn) == ResetReason::PowerOn);
    CHECK(classify(ResetCause::Pin) == ResetReason::Pin);
    CHECK(classify(ResetCause::Brownout) == ResetReason::Brownout);
    CHECK(classify(ResetCause::Software) == ResetReason::Software);
    CHECK(classify(ResetCause::Watchdog) == ResetReason::Watchdog);
    CHECK(classify(ResetCause::Lockup) == ResetReason::Lockup);
    CHECK(classify(ResetCause::LowPowerWake) == ResetReason::LowPowerWake);
    CHECK(classify(ResetCause::Debug) == ResetReason::Debug);

    // A reason with no name reaches the panel as a blank, which is the one
    // outcome a self-test page must never produce.
    for (uint8_t i = 0; i <= static_cast<uint8_t>(ResetReason::Debug); i++) {
        const char* name = to_string(static_cast<ResetReason>(i));
        CHECK(name[0] != '\0');
    }
}
