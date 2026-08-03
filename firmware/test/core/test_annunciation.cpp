// The annunciation policy on its own: given a level, whether it just got worse,
// and the time, what should the buzzer be doing right now.
//
// The bug this file exists for: hal::Annunciator::alarm() opens a continuous
// tone that runs until silence(), and the service only silenced it when the
// worst level reached zero. A level 3 that decayed to level 1 therefore sounded
// at the urgent pitch for as long as anything at all stayed inside the 3 km
// info ring. Every case below is either a pattern that ends by itself or a
// release the service used to miss.
#include "core/annunciation/pattern.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::annunciation;

namespace {

// The service loop's own step. The policy is only ever asked at this rate, so
// the tests ask it at this rate too: a pattern that only comes out right when
// sampled at a millisecond is not a pattern this device can play.
constexpr uint32_t kStepMs = 10;

// What a buzzer would have done, from the commands the policy issued. It counts
// alarm() and silence() calls because that is the difference between one tone
// with a cadence and the same tone re-armed on every pass of the loop.
struct Buzzer {
    Policy policy{};
    bool on{false};
    uint8_t level{0};
    int tone_commands{0};
    int silences{0};
    uint32_t on_ms{0};
    // The beep currently sounding, and the last one that finished.
    uint32_t beep_started_ms{0};
    uint32_t last_beep_ms{0};
    uint32_t last_gap_ms{0};
    uint32_t off_since_ms{0};
    int beeps{0};

    void apply(const Command& command, uint32_t now_ms) {
        if (on) on_ms += kStepMs;
        if (!command.changed) return;
        if (command.tone_on) {
            tone_commands++;
            if (!on) {
                beeps++;
                beep_started_ms = now_ms;
                last_gap_ms = now_ms - off_since_ms;
            }
            on = true;
            level = command.tone_level;
            return;
        }
        silences++;
        if (on) {
            last_beep_ms = now_ms - beep_started_ms;
            off_since_ms = now_ms;
        }
        on = false;
        level = 0;
    }

    // Hold one situation for a while, at the rate the product asks at.
    void run(Situation situation, uint32_t& t, uint32_t ms) {
        const uint32_t until = t + ms;
        for (; t < until; t += kStepMs) apply(policy.update(situation, t), t);
    }

    // The same, with the escalation flag true on the first pass only: that is
    // exactly how the tracker reports one.
    void escalate(Situation situation, uint32_t& t, uint32_t ms) {
        situation.escalated = true;
        apply(policy.update(situation, t), t);
        t += kStepMs;
        situation.escalated = false;
        run(situation, t, ms - kStepMs);
    }
};

Situation standing(uint8_t level) {
    Situation s{};
    s.level = level;
    return s;
}

}  // namespace

TEST_CASE("annunciation: every pattern ends by itself, and the three levels differ by rhythm") {
    // No pattern may be a tone with no end, whatever the level: the pitch may
    // move under a hardware gate, the rhythm is what a pilot learns.
    for (uint8_t level = 1; level <= 3; level++) {
        const Pattern p = pattern_for(Voice::Traffic, level);
        CHECK(p.tone_ms > 0);
        CHECK(p.repeats >= 1);
        if (p.repeats > 1) CHECK(p.gap_ms > 0);
    }
    const Pattern info = pattern_for(Voice::Traffic, 1);
    const Pattern important = pattern_for(Voice::Traffic, 2);
    const Pattern urgent = pattern_for(Voice::Traffic, 3);

    // Count, then rate, then length: three different things to hear.
    CHECK(info.repeats != important.repeats);
    CHECK(important.repeats != urgent.repeats);
    CHECK(urgent.tone_ms < important.tone_ms);
    CHECK(info.tone_ms != important.tone_ms);
    // Only the urgent one keeps talking.
    CHECK(info.reannounce_ms == 0);
    CHECK(important.reannounce_ms == 0);
    CHECK(urgent.reannounce_ms > 0);
}

TEST_CASE("annunciation: level 1 is one discreet blip, not a tone") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.escalate(standing(1), t, 5000);

    CHECK(buzzer.beeps == 1);
    CHECK(buzzer.last_beep_ms == kInfoDiscreetBlipMs);
    CHECK(buzzer.on_ms == kInfoDiscreetBlipMs);
    CHECK_FALSE(buzzer.on);
    // Five seconds of a contact standing at info: said once, and once only.
    CHECK(buzzer.tone_commands == 1);
    CHECK(buzzer.silences == 1);
}

TEST_CASE("annunciation: level 2 is a pair of beeps with a beep-length gap") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.escalate(standing(2), t, 5000);

    CHECK(buzzer.beeps == kImportantPairBeepCount);
    CHECK(buzzer.last_beep_ms == kImportantPairBeepMs);
    CHECK(buzzer.last_gap_ms == kImportantPairGapSameAsBeepMs);
    CHECK(buzzer.on_ms == kImportantPairBeepMs * kImportantPairBeepCount);
    CHECK_FALSE(buzzer.on);
}

TEST_CASE("annunciation: a standing urgent is a pulse train, and it says so again on cadence") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.escalate(standing(3), t, kUrgentStandingReannounceMs);

    CHECK(buzzer.beeps == kUrgentTrainPulseCount);
    CHECK(buzzer.last_beep_ms == kUrgentTrainPulseMs);
    CHECK(buzzer.last_gap_ms == kUrgentTrainGapSameAsPulseMs);
    CHECK_FALSE(buzzer.on);
    // One PWM start per pulse: the tone is not re-armed on every pass of the
    // loop, which would be a click rather than a tone.
    CHECK(buzzer.tone_commands == kUrgentTrainPulseCount);

    // It stands, nothing escalates, and it is announced again on the tracker's
    // own re-notification cadence - three more trains in six seconds.
    buzzer.run(standing(3), t, 3 * kUrgentStandingReannounceMs);
    CHECK(buzzer.beeps == 4 * kUrgentTrainPulseCount);
    CHECK(int(buzzer.policy.announcing_level()) == 3);
    // A train is 990 ms of the two seconds, so there is a second of quiet in
    // every cycle for a radio call, a vario, or the other pilot shouting.
    CHECK(buzzer.on_ms == 4 * kUrgentTrainPulseCount * kUrgentTrainPulseMs);
}

TEST_CASE("annunciation: an urgent that decays to info changes the tone, it does not hold it") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.escalate(standing(3), t, 1500);
    REQUIRE(buzzer.beeps == kUrgentTrainPulseCount);
    const int urgent_beeps = buzzer.beeps;
    const uint32_t urgent_on_ms = buzzer.on_ms;

    // The tracker's hysteresis has let go: what stands now is info. This is the
    // case that used to leave the buzzer sounding at the urgent pitch for the
    // rest of the climb.
    buzzer.run(standing(1), t, 5000);
    CHECK(buzzer.beeps == urgent_beeps + 1);
    CHECK(buzzer.last_beep_ms == kInfoDiscreetBlipMs);
    CHECK(buzzer.on_ms == urgent_on_ms + kInfoDiscreetBlipMs);
    CHECK_FALSE(buzzer.on);
    CHECK(int(buzzer.policy.announcing_level()) == 1);
}

TEST_CASE("annunciation: the buzzer is released the moment its reason goes") {
    // Four ways for the reason to go, and the tone stops for every one of them.
    SUBCASE("the level falls to nothing: the target has gone") {
        Buzzer buzzer;
        uint32_t t = 1000;
        buzzer.escalate(standing(3), t, 200);
        REQUIRE(buzzer.on);
        buzzer.run(standing(0), t, kStepMs);
        CHECK_FALSE(buzzer.on);
        CHECK(buzzer.silences == 2);  // one pulse gap, then the release
        CHECK(int(buzzer.policy.announcing_level()) == 0);

        // And it stays gone: nothing re-announces an empty sky.
        const int commands = buzzer.tone_commands;
        buzzer.run(standing(0), t, 10000);
        CHECK(buzzer.tone_commands == commands);
    }

    SUBCASE("alarms are switched off in settings, mid-pulse") {
        Buzzer buzzer;
        uint32_t t = 1000;
        buzzer.escalate(standing(3), t, 40);
        REQUIRE(buzzer.on);

        Situation off = standing(3);
        off.enabled = false;
        buzzer.run(off, t, kStepMs);
        CHECK_FALSE(buzzer.on);

        // Silent for as long as it is switched off, threat or no threat.
        const int commands = buzzer.tone_commands;
        buzzer.run(off, t, 10000);
        CHECK(buzzer.tone_commands == commands);

        // And it comes back when the pilot turns it on again.
        buzzer.run(standing(3), t, 200);
        CHECK(buzzer.tone_commands > commands);
    }

    SUBCASE("the device is going down, mid-pulse") {
        Buzzer buzzer;
        uint32_t t = 1000;
        buzzer.escalate(standing(3), t, 40);
        REQUIRE(buzzer.on);

        Situation down = standing(3);
        down.running = false;
        buzzer.run(down, t, kStepMs);
        CHECK_FALSE(buzzer.on);
        const int commands = buzzer.tone_commands;
        buzzer.run(down, t, 30000);
        CHECK(buzzer.tone_commands == commands);
    }

    SUBCASE("the fix chirp ends on its own clock, with nothing to release it") {
        Buzzer buzzer;
        uint32_t t = 1000;
        Situation fix{};
        fix.first_fix = true;
        buzzer.apply(buzzer.policy.update(fix, t), t);
        t += kStepMs;
        buzzer.run(standing(0), t, 5000);
        CHECK(buzzer.beeps == 1);
        CHECK(buzzer.last_beep_ms == kFirstFixChirpMs);
        CHECK_FALSE(buzzer.on);
    }
}

TEST_CASE("annunciation: traffic owns the buzzer, the fix chirp only borrows it") {
    // A chirp while traffic stands is refused: one owner at a time, and the
    // pilot's answer to "where is the traffic" is not a chirp.
    Buzzer standing_traffic;
    uint32_t t = 1000;
    standing_traffic.escalate(standing(3), t, 100);
    Situation both = standing(3);
    both.first_fix = true;
    standing_traffic.apply(standing_traffic.policy.update(both, t), t);
    CHECK(standing_traffic.policy.voice() == Voice::Traffic);
    CHECK(int(standing_traffic.policy.announcing_level()) == 3);

    // The other way round: a chirp in progress loses the buzzer the instant
    // traffic wants it. The handover is heard as the pattern changing under the
    // pilot's ear - the chirp's tail becomes the first beep of the pair - and
    // from there the traffic pattern owns the cadence.
    Buzzer chirping;
    uint32_t u = 1000;
    Situation fix{};
    fix.first_fix = true;
    chirping.apply(chirping.policy.update(fix, u), u);
    u += kStepMs;
    chirping.run(Situation{}, u, 100);
    REQUIRE(chirping.on);
    REQUIRE(chirping.policy.voice() == Voice::FirstFix);

    chirping.escalate(standing(2), u, 3000);
    CHECK(chirping.policy.voice() == Voice::Traffic);
    CHECK(int(chirping.policy.announcing_level()) == 2);
    CHECK(chirping.beeps == kImportantPairBeepCount);
    CHECK(chirping.last_beep_ms == kImportantPairBeepMs);
}

TEST_CASE("annunciation: a second escalation to the same level is said again") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.escalate(standing(2), t, 2000);
    REQUIRE(buzzer.beeps == kImportantPairBeepCount);

    // A different aircraft arriving at the same level is news, and the tracker
    // reports it as an escalation. Without the flag the level alone has not
    // changed and nothing would be said.
    buzzer.escalate(standing(2), t, 2000);
    CHECK(buzzer.beeps == 2 * kImportantPairBeepCount);
}
