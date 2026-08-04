// What the buzzer does in a cockpit, through the whole product: virtual
// aircraft transmit real ADS-L frames, the production receive path decodes
// them, the tracker grades them and the alarm service drives the annunciator
// the board is holding. Nothing below the services is stubbed.
//
// The bug: the annunciator's alarm() opens a CONTINUOUS tone and the service
// only ever closed it when the worst level fell to zero. A level 3 that decayed
// to level 1 went on sounding at the urgent pitch for the rest of the climb,
// and there was no cadence at all - one tone, started once, released when the
// sky emptied. In a thermal that is the reason a pilot switches the device off.
#include "core/annunciation/pattern.h"
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kStepMs = simulator::Simulator::kStepMs;

struct Sky {
    simulator::Simulator simulator{};

    platform::host::Annunciator& buzzer() { return simulator.platform().annunciator(); }
    uint32_t tone_commands() { return buzzer().tone_commands(); }
    uint8_t sounding_level() { return simulator.alarm_level(); }
    uint8_t announcing_level() { return simulator.announcing_level(); }

    void run(uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += kStepMs) simulator.step(t);
    }

    // A device flying along with one converging glider on the M-band: urgent
    // within a couple of seconds of the first frames being decoded.
    uint32_t with_an_urgent_threat() {
        REQUIRE(simulator.setup() == Status::Ok);
        run(0, 2000);
        simulator.world().add_threat();
        run(2000, 6000);
        REQUIRE(int(announcing_level()) == 3);
        return 6000;
    }

    // Step until the buzzer is actually mid-pulse, so a release has something
    // to release. Returns the instant, or 0 if the pattern never sounded.
    uint32_t step_until_sounding(uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += kStepMs) {
            simulator.step(t);
            if (sounding_level() > 0) return t;
        }
        return 0;
    }
};

}  // namespace

TEST_CASE("product: an urgent contact is a pulse train, re-announced while it stands") {
    Sky sky;
    uint32_t t = sky.with_an_urgent_threat();

    // Two seconds of a standing urgent: one train of six pulses, and not one
    // PWM start more. The loop made four hundred passes over the same window -
    // a tone re-armed on every pass is a click, not a tone.
    const uint32_t before = sky.tone_commands();
    sky.run(t, t + annunciation::kUrgentStandingReannounceMs);
    t += annunciation::kUrgentStandingReannounceMs;
    CHECK(sky.tone_commands() - before == annunciation::kUrgentTrainPulseCount);

    // And it is a cadence, not a latch: over the same window the buzzer spent
    // more time off than on, which is what leaves room for a radio call.
    int on = 0, off = 0;
    for (uint32_t u = t; u <= t + annunciation::kUrgentStandingReannounceMs; u += kStepMs) {
        sky.simulator.step(u);
        if (sky.sounding_level() > 0)
            on++;
        else
            off++;
    }
    CHECK(on > 0);
    CHECK(off > on);
}

TEST_CASE("product: an urgent that decays to info changes the tone instead of holding it") {
    Sky sky;
    uint32_t t = sky.with_an_urgent_threat();

    // The threat flies through and opens out. It stays inside the 3 km info
    // ring, and it stays on the screen, so the old code kept the urgent tone
    // sounding for as long as it was in sight.
    uint32_t decayed_ms = 0;
    uint32_t commands_at_decay = 0;
    int sounding_after = 0;
    for (; t <= 40000; t += kStepMs) {
        sky.simulator.step(t);
        if (decayed_ms == 0) {
            if (int(sky.announcing_level()) != 1) continue;
            decayed_ms = t;
            commands_at_decay = sky.tone_commands();
            continue;
        }
        if (sky.sounding_level() > 0) sounding_after++;
    }
    REQUIRE(decayed_ms > 0);

    // What follows the decay is the info pattern and nothing else: one discreet
    // blip, begun on the pass the level changed, and then quiet for the rest of
    // the encounter.
    CHECK(sky.tone_commands() == commands_at_decay);
    CHECK(uint32_t(sounding_after) * kStepMs <= annunciation::kInfoDiscreetBlipMs);
    CHECK(int(sky.sounding_level()) == 0);
    // Still traffic, still on the screen, still worth one blip if it worsens.
    CHECK(int(sky.announcing_level()) == 1);
    CHECK(sky.simulator.product().state().traffic.count() == 1);
}

TEST_CASE("product: an empty sky releases the buzzer and does not re-announce anything") {
    Sky sky;
    uint32_t t = sky.with_an_urgent_threat();
    REQUIRE(sky.tone_commands() > 0);

    // Nothing transmits any more. The target ages out of the alert window
    // first, and the traffic table drops it later, on its own schedule.
    sky.simulator.world().clear_aircraft();
    sky.run(t, t + 10000);
    t += 10000;
    CHECK(int(sky.announcing_level()) == 0);
    CHECK(int(sky.sounding_level()) == 0);

    const uint32_t before = sky.tone_commands();
    sky.run(t, t + 20000);
    CHECK(sky.tone_commands() == before);
    CHECK(int(sky.sounding_level()) == 0);
}

TEST_CASE("product: switching alarms off silences the buzzer on the pass it is switched off") {
    Sky sky;
    uint32_t t = sky.with_an_urgent_threat();

    // Mid-pulse, which is the case that matters: a setting that only takes
    // effect at the end of a pattern is a setting a pilot does not believe.
    t = sky.step_until_sounding(t, t + 3000);
    REQUIRE(t > 0);
    const uint32_t silences = sky.buzzer().silences();

    sky.simulator.product().state().settings.alarm_enabled = false;
    sky.simulator.step(t + kStepMs);
    CHECK(int(sky.sounding_level()) == 0);
    CHECK(sky.buzzer().silences() == silences + 1);

    // Silent while it is off, with the threat still closing.
    const uint32_t before = sky.tone_commands();
    sky.run(t, t + 10000);
    CHECK(sky.tone_commands() == before);
    CHECK(int(sky.announcing_level()) == 0);

    // And audible again the moment the pilot turns it back on.
    sky.simulator.product().state().settings.alarm_enabled = true;
    sky.run(t + 10000, t + 13000);
    CHECK(sky.tone_commands() > before);
}

TEST_CASE("product: a device on its way down does not leave the buzzer sounding") {
    Sky sky;
    uint32_t t = sky.with_an_urgent_threat();

    t = sky.step_until_sounding(t, t + 3000);
    REQUIRE(t > 0);

    // The same request a long press makes. From here the service loop stops
    // running, so whatever the buzzer was doing is whatever it would go on
    // doing until the rails drop.
    sky.simulator.product().shutdown().request(power::ShutdownReason::LongPress, t);
    sky.simulator.step(t + kStepMs);
    REQUIRE(sky.simulator.product().shutdown().going_down());
    CHECK(int(sky.sounding_level()) == 0);
    CHECK_FALSE(sky.simulator.product().alarm().sounding());

    const uint32_t before = sky.tone_commands();
    sky.run(t, t + 20000);
    CHECK(sky.tone_commands() == before);
    CHECK(int(sky.sounding_level()) == 0);
    CHECK(int(sky.announcing_level()) == 0);
    // The panel's own peripheral goes the same way: nothing is left driven.
    CHECK_FALSE(sky.simulator.backlight());
}

TEST_CASE("product: a standing urgent buzzes the motor once, not on every re-announcement") {
    Sky sky;
    uint32_t t = sky.with_an_urgent_threat();
    // Haptics mean "this got worse", so they belong to the escalation and to
    // nothing else. The tone says it again every two seconds; the motor in a
    // pocket doing the same would be a pilot's whole flight.
    const uint32_t pulses = sky.buzzer().vibro_pulses();
    CHECK(pulses >= 1);

    sky.run(t, t + 3 * annunciation::kUrgentStandingReannounceMs);
    CHECK(sky.buzzer().vibro_pulses() == pulses);
    CHECK(sky.tone_commands() > 3 * annunciation::kUrgentTrainPulseCount);
}

TEST_CASE("product: the first fix chirps, and traffic takes the buzzer off it") {
    Sky sky;
    REQUIRE(sky.simulator.setup() == Status::Ok);

    // The chirp is one tone that ends on its own clock: nothing has to remember
    // to silence it, and it is the alarm service - the one owner of the
    // annunciator - that plays it.
    uint32_t chirping = 0;
    uint8_t chirp_level = 0;
    uint32_t chirp_ms = 0;
    for (uint32_t t = 0; t <= 3000; t += kStepMs) {
        sky.simulator.step(t);
        if (sky.sounding_level() == 0) continue;
        if (chirping == 0) {
            chirping = t;
            chirp_level = sky.sounding_level();
        }
        chirp_ms += kStepMs;
    }
    CHECK(sky.tone_commands() == 1);
    CHECK(chirping > 0);
    CHECK(int(chirp_level) == annunciation::kFirstFixChirpLevel);
    CHECK(chirp_ms >= annunciation::kFirstFixChirpMs - kStepMs);
    CHECK(chirp_ms <= annunciation::kFirstFixChirpMs);
    // No haptics: the motor is reserved for traffic that got worse, so a pilot
    // who feels it knows what it means without looking.
    CHECK(sky.buzzer().vibro_pulses() == 0);
    CHECK(int(sky.sounding_level()) == 0);
    CHECK(sky.buzzer().silences() == 1);

    // Traffic then owns it, and the fix is not chirped a second time.
    sky.simulator.world().add_threat();
    sky.run(3000, 7000);
    CHECK(int(sky.announcing_level()) == 3);
}

// ---------------------------------------------------------------------------
// The haptic, all the way down to the registers.
//
// Every case above counts pulses at the role. That counter was true and the
// device still did not vibrate: the annunciator drove P0.08 as a plain GPIO, and
// on a T-Echo Plus that pin is a DRV2605's enable. These drive the whole product
// and then read the chip.
// ---------------------------------------------------------------------------

TEST_CASE("product: an escalation reaches the haptic driver's registers") {
    Sky sky;
    models::Drv2605& chip = sky.simulator.platform().chips().haptic;

    // Configured at bring-up and idle: standby, not a pin sitting high.
    REQUIRE(sky.simulator.setup() == Status::Ok);
    CHECK(chip.standby());
    CHECK_FALSE(chip.moving());

    sky.run(0, 2000);
    sky.simulator.world().add_threat();

    bool moved = false;
    for (uint32_t t = 2000; t <= 8000; t += kStepMs) {
        sky.simulator.step(t);
        if (chip.moving()) moved = true;
    }
    CHECK(int(sky.announcing_level()) == 3);
    // The pulse was made over the bus: a mode, a drive value and a stop. The
    // enable pin is not wired into the host's virtual GPIO at all
    // (hardware/platform/host/io.h), so nothing here could have moved the motor
    // by driving P0.08 - which is what the annunciator used to do.
    CHECK(moved);
    CHECK(sky.buzzer().vibro_pulses() >= 1);
}

TEST_CASE("product: the motor is not left running after its pulse") {
    Sky sky;
    models::Drv2605& chip = sky.simulator.platform().chips().haptic;
    uint32_t t = sky.with_an_urgent_threat();

    // A pulse is 600 ms at urgent (services/alarm.h). Well past it, the driver is
    // back in standby: a motor left on is a flat battery, and the DRV2605's own
    // drive stage is milliamps.
    sky.run(t, t + 3000);
    CHECK_FALSE(chip.moving());
    CHECK(chip.standby());
}

TEST_CASE("product: a unit with no haptic driver flies, sounds, and says what is missing") {
    // The same board with nothing at 0x5A: an empty pad, a dead part, or a plain
    // T-Echo that came down the line as a Plus.
    constexpr hal::Capabilities kNoHaptic = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Vibro));
    Rig rig{kNoHaptic};
    REQUIRE(rig.setup() == Status::Ok);

    // Optional, so the device flies and says so once.
    CHECK(rig.product.flyable());
    CHECK_FALSE(hal::has(rig.product.capabilities(), hal::Capability::Vibro));
    CHECK(hal::has(rig.product.degraded(), hal::Capability::Vibro));

    // And the voice it does have still works: the first fix is chirped by the
    // same service that would have pulsed the motor.
    uint32_t t = 0;
    rig.push_timed_fix(/*speed_q=*/0, /*alt_msl_m=*/300);
    rig.run(t, t + 1000);
    CHECK(rig.platform.annunciator().tone_commands() >= 1);
    CHECK(rig.platform.chips().haptic.moving() == false);
}
