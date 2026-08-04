// The status lamp through the whole product: the cell as the divider reads it, the
// cable, the fix as the receiver reports it, and virtual aircraft transmitting real
// ADS-L frames for the one row that outranks a charger. Nothing below the services
// is stubbed.
//
// The finding this suite exists for: e-paper holds its last image with the rails
// down, so until this landed there was no way at all to tell a running device from
// an off one - the T-Echo's three LEDs appeared nowhere in the tree. Two properties
// matter more than any colour and both are asserted here: the lamp is dark when the
// device is going down (a lit LED on a device that is off is the bug, not a
// cosmetic slip), and a unit with no lamp fitted runs the same table and lights
// nothing.
#include "core/indication/lamp.h"
#include "core/power/cutoff.h"
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

platform::host::Indicator& lamp_of(Rig& rig) { return rig.platform.indicator(); }

// A device that has been running long enough for the gauge to have medianed three
// samples, so the cutoff monitor has a level and not Unknown.
void settle(Rig& rig, uint32_t& t, uint32_t for_ms = 5000) {
    rig.run(t, t + for_ms);
    t += for_ms;
}

// Runs until the lamp is actually mid-flash, so a test that means "green" is not
// reading the dark half of a wink. Returns the instant, or 0.
uint32_t step_until_lit(Rig& rig, uint32_t& t, uint32_t within_ms) {
    for (uint32_t end = t + within_ms; t <= end; t += 10) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        if (lamp_of(rig).lit()) return t;
    }
    return 0;
}

}  // namespace

TEST_CASE("product: a running device with no fix winks blue, and green once it has one") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    settle(rig, t);

    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::NoFix);
    REQUIRE(step_until_lit(rig, t, 3500) > 0);
    CHECK(lamp_of(rig).lamp() == hal::Lamp::Blue);

    rig.push_fix(/*alt_m=*/500, /*updates=*/1);
    settle(rig, t, 500);
    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::Alive);
    REQUIRE(step_until_lit(rig, t, 3500) > 0);
    CHECK(lamp_of(rig).lamp() == hal::Lamp::Green);
}

TEST_CASE("product: a healthy device holds its lamp dark almost all the time") {
    // The whole cost argument, measured rather than asserted: an LED at 1% is not
    // a power term, an LED left on is. Sampled at the loop's own rate over a
    // minute of flying.
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_fix(/*alt_m=*/500, /*updates=*/1);

    uint32_t lit_passes = 0;
    uint32_t passes = 0;
    for (uint32_t t = 0; t <= 60000; t += 10) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        passes++;
        if (lamp_of(rig).lit()) lit_passes++;
    }
    REQUIRE(passes > 0);
    const uint32_t duty_permille = 1000u * lit_passes / passes;
    MESSAGE("alive duty measured through the product: ", duty_permille, "/1000");
    CHECK(duty_permille <= indication::kSteadyDutyCeilingPermille);
    // And it did light: a lamp that is cheap because it never comes on is not an
    // indicator.
    CHECK(lamp_of(rig).lightings() >= 15);
}

TEST_CASE("product: a cell below the warning level blinks the lamp red") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_fix(/*alt_m=*/500, /*updates=*/1);
    uint32_t t = 0;
    settle(rig, t);
    REQUIRE(rig.product.alarm().indicator_condition() == indication::Condition::Alive);

    // Below kLowWarnMv and above the cutoff, so the device warns and keeps flying.
    rig.platform.battery().millivolts = power::kLowWarnMv - 100;
    settle(rig, t, 5000);
    REQUIRE(rig.state().power_level == power::PowerLevel::Low);
    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::Low);
    REQUIRE(step_until_lit(rig, t, 1000) > 0);
    CHECK(lamp_of(rig).lamp() == hal::Lamp::Red);
}

TEST_CASE("product: a divider that reads nothing does not blink like a flat cell") {
    // An unpopulated or unconnected divider drifts near zero, which the sanity
    // floor throws away, leaving the level Unknown for ever. Unknown is not low.
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_fix(/*alt_m=*/500, /*updates=*/1);
    rig.platform.battery().millivolts = 200;
    uint32_t t = 0;
    settle(rig, t, 8000);

    CHECK(rig.state().power_level == power::PowerLevel::Unknown);
    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::Alive);
}

TEST_CASE("product: a cable in shows charging, and green when the charge has finished") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_fix(/*alt_m=*/500, /*updates=*/1);
    uint32_t t = 0;
    settle(rig, t);

    rig.platform.battery().external_power = true;
    rig.platform.battery().millivolts = 4000;
    settle(rig, t, 5000);
    REQUIRE(rig.state().battery.charging);
    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::Charging);
    // Held, not winked: external power is paying, and a pilot holding the cable
    // wants an answer that does not need watching for three seconds.
    CHECK(lamp_of(rig).lamp() == hal::Lamp::Red);
    const uint32_t shows = lamp_of(rig).shows();
    settle(rig, t, 5000);
    CHECK(lamp_of(rig).shows() == shows);
    CHECK(lamp_of(rig).lamp() == hal::Lamp::Red);

    // The charger has stopped pushing current and is holding the float voltage.
    rig.platform.battery().millivolts = power::kChargeCompleteMv + 5;
    settle(rig, t, 5000);
    REQUIRE_FALSE(rig.state().battery.charging);
    REQUIRE(rig.state().battery.external_power);
    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::Charged);
    CHECK(lamp_of(rig).lamp() == hal::Lamp::Green);
}

TEST_CASE("product: a low cell on the cable shows charging, not low") {
    // The first minute of a charge is a low cell with external power present. The
    // useful answer is the one the pilot plugged the cable in to get.
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.platform.battery().millivolts = power::kLowWarnMv - 200;
    settle(rig, t, 6000);
    REQUIRE(rig.state().power_level == power::PowerLevel::Low);
    REQUIRE(rig.product.alarm().indicator_condition() == indication::Condition::Low);

    rig.platform.battery().external_power = true;
    settle(rig, t, 5000);
    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::Charging);
    CHECK(lamp_of(rig).lamp() == hal::Lamp::Red);
}

TEST_CASE("product: a device on its way down darkens the lamp and lets go of the pins") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().external_power = true;
    uint32_t t = 0;
    settle(rig, t);
    REQUIRE(lamp_of(rig).lit());

    // The same request a long press makes. From here the service loop stops
    // running, so whatever the lamp was doing is whatever it would go on doing
    // until the rails drop - and these LEDs are active-low, so "off" is a pin
    // driven high into a rail that is about to collapse.
    rig.product.shutdown().request(power::ShutdownReason::LongPress, t);
    rig.run(t, t + 100);
    REQUIRE(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);

    CHECK_FALSE(lamp_of(rig).lit());
    CHECK(lamp_of(rig).parked());
    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::Off);

    // And nothing brings it back: the loop is not running, and a cable still in
    // is not a reason to light a device that is going off.
    const uint32_t lightings = lamp_of(rig).lightings();
    rig.run(t + 100, t + 20000);
    CHECK(lamp_of(rig).lightings() == lightings);
    CHECK_FALSE(lamp_of(rig).lit());
}

TEST_CASE("product: a unit with no status LED flies and lights nothing") {
    // Absent hardware is a capability, not a null pointer and not a second code
    // path: the table still runs, the condition is still decided, and the port
    // hal/indicator.h defines - which IS the absent lamp - swallows it.
    constexpr hal::Capabilities kNoLamp = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Indicator));
    Rig rig{kNoLamp};
    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(hal::has(rig.product.capabilities(), hal::Capability::Indicator));
    CHECK(hal::has(rig.product.degraded(), hal::Capability::Indicator));

    rig.push_fix(/*alt_m=*/500, /*updates=*/1);
    uint32_t t = 0;
    settle(rig, t, 10000);

    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::Alive);
    // The board's lamp was never attached, so nothing reached it at all.
    CHECK(lamp_of(rig).shows() == 0);
    CHECK_FALSE(lamp_of(rig).lit());
}

TEST_CASE("product: switching alarms off silences the buzzer and does not darken the lamp") {
    // "Is this thing on" is not a preference. A pilot who turned the noise off did
    // not ask to lose the only way there is of telling a live device from a dead
    // one, which is why settings.alarm_enabled reaches core/annunciation and is not
    // a field in core/indication's situation at all.
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.state().settings.alarm_enabled = false;
    rig.push_fix(/*alt_m=*/500, /*updates=*/1);
    uint32_t t = 0;
    settle(rig, t);

    CHECK(rig.product.alarm().indicator_condition() == indication::Condition::Alive);
    REQUIRE(step_until_lit(rig, t, 3500) > 0);
    CHECK(lamp_of(rig).lamp() == hal::Lamp::Green);
    CHECK_FALSE(rig.product.alarm().sounding());
}

TEST_CASE("product: an urgent contact takes the lamp off the charger") {
    // The one row that outranks a cable, driven by real frames on the air rather
    // than by a level set by hand: a device on a powered install still warns.
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    platform::host::Indicator& lamp = simulator.platform().indicator();
    simulator.platform().battery().external_power = true;

    constexpr uint32_t kStepMs = simulator::Simulator::kStepMs;
    auto run = [&](uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += kStepMs) simulator.step(t);
    };

    run(0, 2000);
    REQUIRE(simulator.product().alarm().indicator_condition() == indication::Condition::Charging);
    REQUIRE(lamp.lamp() == hal::Lamp::Red);

    simulator.world().add_threat();
    run(2000, 6000);
    REQUIRE(int(simulator.alarm_level()) >= indication::kAlarmTakesLamp);
    CHECK(simulator.product().alarm().indicator_condition() == indication::Condition::Alarm);

    // Red either way, so the colour is not the evidence: the rhythm is. Charging
    // is held and told once; the alarm flickers, so the lamp is told repeatedly.
    const uint32_t shows = lamp.shows();
    run(6000, 8000);
    CHECK(lamp.shows() - shows >= 10);
}
