// core/indication/lamp.h: THE state-to-indicator table. One table, one priority
// order, one place support looks.
//
// Why this is not core/annunciation, which is the obvious place to put it and the
// wrong one. Both files answer "what is the device telling the pilot", so the
// case for merging them is real, and it loses on four counts:
//
//   1. Annunciation announces EVENTS and every pattern it plays ends by itself -
//      that is the bug that file exists to not have. This announces a LEVEL, it
//      never ends, and the whole design question is what it costs to hold.
//   2. The inputs do not overlap. Annunciation's situation is a traffic level,
//      whether it just got worse, and the first fix. This one's is the cell, the
//      charger, the fix and the alarm, i.e. most of core/power plus one field.
//   3. settings.alarm_enabled silences the buzzer. It must not darken the lamp:
//      "is this thing on" is not a preference, and a pilot who turned the noise
//      off did not ask to be unable to tell a live device from a dead one.
//   4. Arbitration is the opposite shape. Annunciation hands one voice to
//      another and has to decide who may interrupt whom. Here every condition is
//      true or not true at the same instant and exactly one wins, which is a
//      priority order over a closed set - kTable below, top row first.
//
// So: two files, and this one depends on the other's output (the alarm level) and
// on nothing else of it. The audible half of the first fix already landed and is
// not repeated here.
//
// INFO: fc 06aug26 SoftRF's status LED is the reference vocabulary and we keep
// its distinction and not its duty: solid above the low threshold, blinking at a
// 300 ms toggle below it (src/driver/LED.cpp:204-219). Solid is an LED left on,
// which on an 850 mAh pack is a real term, so healthy is a wink at 1% and the low
// state keeps SoftRF's rate (600 ms period) at a tenth of its duty. The two rows
// that ARE held are the two where a cable is paying.
#ifndef SKYBLIP_CORE_INDICATION_LAMP_H
#define SKYBLIP_CORE_INDICATION_LAMP_H

#include <cstdint>

#include "core/power/cutoff.h"
#include "hal/indicator.h"

namespace skyblip::indication {

// The five states item F names - alive, charging, low, no fix, alarm - plus the
// two the table needs to be total: a charge that has finished, because a cell
// held at the float voltage is not a cell taking current and a pilot wants to
// know which, and a device on its way down, because dark has to be a row like
// any other rather than the absence of one.
enum class Condition : uint8_t { Off, Alarm, Charging, Charged, Low, NoFix, Alive, kCount };

// An LED reaches full brightness in microseconds, so core/annunciation's 90 ms
// floor - an ear figure, the shortest blip a pilot can place and count - does not
// apply. This is the eye figure: a flash shorter than about this is missed by a
// glance rather than seen dimly.
constexpr uint16_t kShortestFlashEyeCanCatchMs = 20;

// off_ms == 0 means held. The value is written rather than left at zero so that
// duty_permille() below needs no special case for it: 1000/(1000+0) is 1000.
constexpr uint16_t kHeldOnMs = 1000;

struct Indication {
    hal::Lamp lamp{hal::Lamp::None};
    uint16_t on_ms{0};
    uint16_t off_ms{0};
};

// Whose budget the row is spending. This is the honest half of "keep it cheap":
// the ceiling a row is held to is a property of the row, so a test walks the
// table instead of a comment claiming the numbers are small.
enum class Budget : uint8_t {
    // Nothing lit.
    Dark,
    // Held for hours, on the pack. Has to be very nearly free.
    Steady,
    // Minutes at most, on the pack: an alarm that stands, a cell about to go.
    Transient,
    // External power is present, so the lamp is not on the pack's bill at all.
    // This is the one honest reason a row may be held solid.
    Corded,
};

constexpr uint16_t kSteadyDutyCeilingPermille = 20;
constexpr uint16_t kTransientDutyCeilingPermille = 300;

struct Row {
    Condition condition;
    Indication indication;
    Budget budget;
    // One line, in the words a support call uses. Never null.
    const char* meaning;
};

constexpr int kRowCount = static_cast<int>(Condition::kCount);

// THE TABLE. Row order IS the priority order, highest first, and condition_for()
// below is the same order written as code. Two rows deserve their reason here
// rather than in the string a pilot reads:
//
// Alarm outranks charging. A device on a cable in a cockpit is a powered install,
// not a device in a bag, and it still owes a pilot the warning; the answer to
// "an alarm during charging" is that the alarm wins, and it is written here so
// nobody has to ask again.
//
// Charging outranks low. Low-while-charging is the expected state of a cell that
// has just been plugged in, and "it is charging" is the more useful of the two
// answers to the pilot standing there holding the cable.
inline constexpr Row kTable[kRowCount] = {
    {Condition::Off, {hal::Lamp::None, 0, 0}, Budget::Dark, "dark: off, or on its way down"},
    {Condition::Alarm,
     {hal::Lamp::Red, 45, 135},
     Budget::Transient,
     "red, fast flicker: traffic alarm, level 2 or 3"},
    {Condition::Charging,
     {hal::Lamp::Red, kHeldOnMs, 0},
     Budget::Corded,
     "red, held: external power in, cell taking charge"},
    {Condition::Charged,
     {hal::Lamp::Green, kHeldOnMs, 0},
     Budget::Corded,
     "green, held: external power in, cell full"},
    {Condition::Low,
     {hal::Lamp::Red, 60, 540},
     Budget::Transient,
     "red, blinking twice a second: cell below the warning level"},
    {Condition::NoFix,
     {hal::Lamp::Blue, 30, 2970},
     Budget::Steady,
     "blue, one wink every 3 s: running, no GNSS fix yet"},
    {Condition::Alive,
     {hal::Lamp::Green, 30, 2970},
     Budget::Steady,
     "green, one wink every 3 s: running, fix valid"},
};

// Per mille of the time the lamp is lit. Zero when nothing is, 1000 when it is
// held.
constexpr uint16_t duty_permille(const Indication& indication) {
    const uint32_t cycle = static_cast<uint32_t>(indication.on_ms) + indication.off_ms;
    if (indication.lamp == hal::Lamp::None || indication.on_ms == 0 || cycle == 0) return 0;
    return static_cast<uint16_t>(1000u * indication.on_ms / cycle);
}

constexpr uint16_t duty_ceiling_permille(Budget budget) {
    switch (budget) {
        case Budget::Dark: return 0;
        case Budget::Steady: return kSteadyDutyCeilingPermille;
        case Budget::Transient: return kTransientDutyCeilingPermille;
        case Budget::Corded:
        default: return 1000;
    }
}

// The enum is closed and the table has one row per value, so "no duplicates" and
// "every condition present" are the same statement.
constexpr bool table_lists_each_condition_once() {
    for (int i = 0; i < kRowCount; i++)
        for (int j = i + 1; j < kRowCount; j++)
            if (kTable[i].condition == kTable[j].condition) return false;
    return true;
}

constexpr bool every_row_is_inside_its_budget() {
    for (int i = 0; i < kRowCount; i++)
        if (duty_permille(kTable[i].indication) > duty_ceiling_permille(kTable[i].budget))
            return false;
    return true;
}

// The shortest phase any row asks the service loop to resolve, held rows and the
// dark row excluded. A product asserts its own step rate against it.
constexpr uint16_t shortest_phase_ms() {
    uint16_t shortest = 0xFFFF;
    for (int i = 0; i < kRowCount; i++) {
        const Indication& indication = kTable[i].indication;
        if (indication.lamp == hal::Lamp::None || indication.off_ms == 0) continue;
        if (indication.on_ms < shortest) shortest = indication.on_ms;
        if (indication.off_ms < shortest) shortest = indication.off_ms;
    }
    return shortest;
}

constexpr uint16_t kShortestPhaseMs = shortest_phase_ms();

static_assert(table_lists_each_condition_once(),
              "a condition was added to the enum and not to the table, or listed twice");
static_assert(every_row_is_inside_its_budget(),
              "a row spends more of the pack than the budget it declares");
static_assert(kTable[0].condition == Condition::Off,
              "a device on its way down outranks every reason to be lit");
static_assert(kTable[1].condition == Condition::Alarm,
              "an alarm during charging shows the alarm: a powered install still warns");
static_assert(kShortestPhaseMs >= kShortestFlashEyeCanCatchMs, "a flash a glance would miss");

const Indication& indication_for(Condition condition);
const char* to_string(Condition condition);
const char* to_string(hal::Lamp lamp);
// The line support reads. Same string the table carries; a second accessor so a
// caller that has a condition and no table does not go looking for one.
const char* meaning_of(Condition condition);

// Everything the decision depends on, gathered by the one service that owns the
// indicator and passed in whole. Every field is already published on bus::State
// by some other service; nothing here is derived twice.
struct Situation {
    // False from the moment the device starts going down.
    bool running{true};
    // The worst level standing, the same number the panel uses to decide whether
    // the settings page gives the glass back. NOT the announced level: a lamp
    // that went dark in the gaps of the buzzer's pulse train would be reporting
    // the cadence of the sound rather than the presence of the threat.
    uint8_t alarm_level{0};
    bool external_power{false};
    // External power in and the cell at the float voltage: the charger has
    // finished. core/power/battery.h owns the distinction; this only reads it.
    bool charge_complete{false};
    // What the cutoff monitor made of the samples. Read rather than re-derived,
    // so the lamp says LOW at exactly the voltage the panel and the tablet do,
    // with the same debounce and the same sanity floor.
    power::PowerLevel power_level{power::PowerLevel::Unknown};
    bool fix_valid{false};
};

// The level at which traffic takes the lamp. Level 1 is heard dozens of times in
// one thermal and a red flicker for each of them is both expensive and noise, so
// this is the same threshold the panel uses to take the glass back and the alarm
// service uses to reach the haptic.
constexpr uint8_t kAlarmTakesLamp = 2;

// The priority order, as code. This is the only function allowed to know it.
Condition condition_for(const Situation& situation);

struct Command {
    hal::Lamp lamp{hal::Lamp::None};
    // The lamp has to be told something different from what it was last told.
    // An LED re-driven on every pass is a register write a hundred times a
    // second for no light.
    bool changed{false};
};

class Policy {
   public:
    Command update(const Situation& situation, uint32_t now_ms);

    Condition condition() const { return condition_; }
    // What is lit right now, gaps included: None during the off phase of a wink.
    hal::Lamp lamp() const { return shown_; }

   private:
    void advance(uint32_t now_ms);

    Condition condition_{Condition::Off};
    hal::Lamp shown_{hal::Lamp::None};
    uint32_t phase_ms_{0};
    bool lit_{false};
    bool started_{false};
};

}  // namespace skyblip::indication

#endif
