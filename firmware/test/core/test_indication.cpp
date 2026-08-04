// The state-to-indicator table on its own: given the cell, the cable, the fix and
// the alarm, what should the one lamp on this device be doing right now.
//
// Why this suite exists at all. E-paper holds its last image with the rails down,
// so an off device and a running device look identical, and until this landed the
// T-Echo's three LEDs appeared nowhere in the tree. Two things follow, and both
// are asserted below rather than described: the priority order is not a matter of
// opinion (an alarm during charging shows the alarm), and the cost of lighting an
// LED on an 850 mAh pack is a number, not a reassurance - every row declares whose
// budget it spends and no row is allowed to exceed it.
//
// The first case prints the whole table. That is deliberate: this is the one
// place support can read what a colour means, and `make test` output is where it
// is legible without a C++ compiler.
#include <string>

#include "core/indication/lamp.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::indication;

namespace {

// The service loop's own step. The policy is only ever asked at this rate, so the
// tests ask it at this rate too: a wink that only comes out right when sampled at
// a millisecond is not a wink this device can show.
constexpr uint32_t kStepMs = 10;

// A situation that is true of every lit row at once. Peeling one field off it at a
// time is how the priority order gets asserted instead of restated.
Situation everything_at_once() {
    Situation s{};
    s.running = true;
    s.alarm_level = 3;
    s.external_power = true;
    s.charge_complete = false;
    s.power_level = power::PowerLevel::Cutoff;
    s.fix_valid = false;
    return s;
}

// What a lamp would have done, from the commands the policy issued. It integrates
// the lit time because duty cycle is the whole power argument and a claim about
// duty that nothing measures is a comment.
struct Lamp {
    Policy policy{};
    hal::Lamp shown{hal::Lamp::None};
    uint32_t shows{0};
    uint32_t lit_ms{0};
    uint32_t total_ms{0};
    uint32_t flashes{0};

    void step(const Situation& situation, uint32_t now_ms) {
        const Command command = policy.update(situation, now_ms);
        if (shown != hal::Lamp::None) lit_ms += kStepMs;
        total_ms += kStepMs;
        if (!command.changed) return;
        shows++;
        if (command.lamp != hal::Lamp::None && shown == hal::Lamp::None) flashes++;
        shown = command.lamp;
    }

    void run(const Situation& situation, uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += kStepMs) step(situation, t);
    }

    uint32_t duty_permille() const { return total_ms == 0 ? 0 : 1000u * lit_ms / total_ms; }
};

}  // namespace

TEST_CASE("indication: the whole state-to-indicator table, in priority order") {
    for (int i = 0; i < kRowCount; i++) {
        const Row& row = kTable[i];
        MESSAGE(i, ": ", std::string(to_string(row.condition)), " -> ",
                std::string(to_string(row.indication.lamp)), " on=", row.indication.on_ms,
                "ms off=", row.indication.off_ms,
                "ms duty=", indication::duty_permille(row.indication),
                "/1000 : ", std::string(row.meaning));
        REQUIRE(row.meaning != nullptr);
        CHECK(row.meaning[0] != 0);
        // The accessors and the row have to agree, or support reads one thing and
        // the device does another.
        CHECK(indication_for(row.condition).lamp == row.indication.lamp);
        CHECK(std::string(meaning_of(row.condition)) == row.meaning);
    }
    CHECK(kRowCount == static_cast<int>(Condition::kCount));
}

TEST_CASE("indication: no row spends more of the pack than the budget it declares") {
    for (int i = 0; i < kRowCount; i++) {
        const Row& row = kTable[i];
        const uint16_t duty = indication::duty_permille(row.indication);
        CHECK(duty <= duty_ceiling_permille(row.budget));
        // Corded is the only licence to hold a lamp solid, and it is licence
        // because a cable is paying: nothing on the pack may be held.
        if (row.budget != Budget::Corded && row.budget != Budget::Dark)
            CHECK(row.indication.off_ms > 0);
        // A flash the eye would miss is a lamp that costs and says nothing.
        if (row.indication.lamp != hal::Lamp::None)
            CHECK(row.indication.on_ms >= kShortestFlashEyeCanCatchMs);
    }
}

TEST_CASE("indication: an alarm during charging shows the alarm") {
    // The question item F refuses to leave open. A device on a cable in a cockpit
    // is a powered install, not a unit in a flight bag, and it still owes a pilot
    // the warning.
    Situation s = everything_at_once();
    CHECK(condition_for(s) == Condition::Alarm);
    CHECK(indication_for(Condition::Alarm).lamp == hal::Lamp::Red);

    // And level 1 does not: it is heard dozens of times in one thermal.
    s.alarm_level = 1;
    CHECK(condition_for(s) != Condition::Alarm);
    s.alarm_level = kAlarmTakesLamp;
    CHECK(condition_for(s) == Condition::Alarm);
}

TEST_CASE("indication: the priority order is the table, top row first") {
    Situation s = everything_at_once();

    // Off outranks every reason to be lit: after park() nothing runs to advance a
    // blink anyway, and a lamp left showing anything is a device that looks on.
    s.running = false;
    CHECK(condition_for(s) == Condition::Off);
    s.running = true;

    CHECK(condition_for(s) == Condition::Alarm);
    s.alarm_level = 0;

    // Charging outranks a low cell, because a low cell with the cable in is
    // exactly what the first minute of a charge looks like, and "it is charging"
    // is the more useful of the two answers.
    CHECK(condition_for(s) == Condition::Charging);
    s.charge_complete = true;
    CHECK(condition_for(s) == Condition::Charged);
    s.external_power = false;
    s.charge_complete = false;

    CHECK(condition_for(s) == Condition::Low);
    s.power_level = power::PowerLevel::Normal;

    CHECK(condition_for(s) == Condition::NoFix);
    s.fix_valid = true;
    CHECK(condition_for(s) == Condition::Alive);
}

TEST_CASE("indication: the cutoff monitor decides what low means, not a second threshold") {
    Situation s{};
    s.power_level = power::PowerLevel::Low;
    CHECK(condition_for(s) == Condition::Low);
    s.power_level = power::PowerLevel::Cutoff;
    CHECK(condition_for(s) == Condition::Low);
    // A unit whose divider is unpopulated reads Unknown for ever. It is not a low
    // cell and must not blink like one: the whole point of reading the published
    // level rather than comparing millivolts again.
    s.power_level = power::PowerLevel::Unknown;
    CHECK(condition_for(s) == Condition::NoFix);
}

TEST_CASE("indication: alive is a wink, not an LED left on") {
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = true;

    Lamp lamp;
    lamp.run(s, 0, 30000);
    CHECK(lamp.policy.condition() == Condition::Alive);
    // Thirty seconds of a healthy device: ten winks, and the lamp is dark for
    // more than 97% of it.
    CHECK(lamp.duty_permille() <= kSteadyDutyCeilingPermille);
    CHECK(lamp.flashes >= 9);
    CHECK(lamp.flashes <= 11);
}

TEST_CASE("indication: no fix is the same wink in another colour") {
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = false;

    Lamp lamp;
    lamp.run(s, 0, 30000);
    CHECK(lamp.policy.condition() == Condition::NoFix);
    CHECK(lamp.duty_permille() <= kSteadyDutyCeilingPermille);
    CHECK(indication_for(Condition::NoFix).lamp == hal::Lamp::Blue);
    // Same rhythm, so a pilot reads the colour and not a count of flashes.
    CHECK(indication_for(Condition::NoFix).on_ms == indication_for(Condition::Alive).on_ms);
    CHECK(indication_for(Condition::NoFix).off_ms == indication_for(Condition::Alive).off_ms);
}

TEST_CASE("indication: a low cell keeps SoftRF's blink rate at a tenth of its duty") {
    // SoftRF toggles the status LED every 300 ms below the low threshold
    // (src/driver/LED.cpp:204-219), which is a 600 ms period at half duty. We keep
    // the rate, because that is the rate a pilot has been taught to read as
    // trouble, and not the duty: solid-ish is the term this item exists to avoid.
    const Indication& low = indication_for(Condition::Low);
    CHECK(low.on_ms + low.off_ms == 600);
    CHECK(indication::duty_permille(low) <= 100);

    Situation s{};
    s.power_level = power::PowerLevel::Low;
    Lamp lamp;
    lamp.run(s, 0, 6000);
    CHECK(lamp.policy.condition() == Condition::Low);
    CHECK(lamp.flashes >= 9);
    CHECK(lamp.duty_permille() <= kTransientDutyCeilingPermille);
}

TEST_CASE("indication: the two charge rows are the only held ones, and a cable pays for them") {
    for (int i = 0; i < kRowCount; i++) {
        const bool held =
            kTable[i].indication.off_ms == 0 && kTable[i].indication.lamp != hal::Lamp::None;
        const bool corded = kTable[i].budget == Budget::Corded;
        CHECK(held == corded);
    }

    Situation s{};
    s.external_power = true;
    Lamp lamp;
    lamp.run(s, 0, 5000);
    CHECK(lamp.policy.condition() == Condition::Charging);
    CHECK(lamp.shown == hal::Lamp::Red);
    // Held means told once: an LED re-driven every pass is a register write a
    // hundred times a second for no light.
    CHECK(lamp.shows == 1);

    s.charge_complete = true;
    lamp.run(s, 5000, 10000);
    CHECK(lamp.policy.condition() == Condition::Charged);
    CHECK(lamp.shown == hal::Lamp::Green);
    CHECK(lamp.shows == 2);
}

TEST_CASE("indication: the lamp goes dark the moment the device starts going down") {
    Situation s{};
    s.external_power = true;
    Lamp lamp;
    lamp.run(s, 0, 1000);
    REQUIRE(lamp.shown == hal::Lamp::Red);

    s.running = false;
    lamp.step(s, 1010);
    CHECK(lamp.shown == hal::Lamp::None);
    CHECK(lamp.policy.condition() == Condition::Off);
    // And it stays dark: nothing about the cell or the sky brings it back while
    // the device is on its way down.
    s.alarm_level = 3;
    lamp.run(s, 1010, 5000);
    CHECK(lamp.shown == hal::Lamp::None);
}

TEST_CASE("indication: a change shows itself at once, not at the end of the cycle") {
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = true;
    Lamp lamp;
    lamp.run(s, 0, 1000);
    // Mid-cycle: the wink is long over and the lamp is dark for another 2 s.
    REQUIRE(lamp.shown == hal::Lamp::None);

    s.alarm_level = 3;
    lamp.step(s, 1010);
    CHECK(lamp.shown == hal::Lamp::Red);
}

TEST_CASE("indication: the lamp is told only when the answer changes") {
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = true;
    Lamp lamp;
    lamp.run(s, 0, 30000);
    // Ten winks in thirty seconds is twenty commands, not three thousand.
    CHECK(lamp.shows <= 2 * (lamp.flashes + 1));
}

TEST_CASE("indication: the millisecond counter wrapping costs one flash, not a stuck lamp") {
    // hal::Clock::millis() is 32-bit and wraps at 49.7 days. Everything here is
    // unsigned subtraction, so the wrap restarts a wink; an absolute comparison
    // would have left the lamp in whichever phase it was in for ever.
    constexpr uint32_t kJustBeforeWrap = 0xFFFFF000u;
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = true;

    Lamp lamp;
    for (uint32_t t = kJustBeforeWrap; t != 0x00002000u; t += kStepMs) lamp.step(s, t);
    const uint32_t flashes_across_the_wrap = lamp.flashes;
    lamp.run(s, 0x00002000u, 0x00002000u + 30000);
    CHECK(lamp.flashes > flashes_across_the_wrap);
    CHECK(lamp.duty_permille() <= kSteadyDutyCeilingPermille);
}

TEST_CASE("indication: every condition has a name and a lamp that can be named") {
    for (int i = 0; i < kRowCount; i++) {
        CHECK(std::string(to_string(kTable[i].condition)) != "?");
        CHECK(std::string(to_string(kTable[i].indication.lamp)) != "?");
    }
    CHECK(std::string(to_string(hal::Lamp::None)) == "dark");
}
