#include "core/indication/lamp.h"

namespace skyblip::indication {

const Indication& indication_for(Condition condition) {
    for (int i = 0; i < kRowCount; i++)
        if (kTable[i].condition == condition) return kTable[i].indication;
    return kTable[0].indication;
}

const char* meaning_of(Condition condition) {
    for (int i = 0; i < kRowCount; i++)
        if (kTable[i].condition == condition) return kTable[i].meaning;
    return kTable[0].meaning;
}

const char* to_string(Condition condition) {
    switch (condition) {
        case Condition::Off: return "off";
        case Condition::Alarm: return "alarm";
        case Condition::Charging: return "charging";
        case Condition::Charged: return "charged";
        case Condition::Low: return "low";
        case Condition::NoFix: return "no-fix";
        case Condition::Alive: return "alive";
        default: return "?";
    }
}

const char* to_string(hal::Lamp lamp) {
    switch (lamp) {
        case hal::Lamp::Green: return "green";
        case hal::Lamp::Red: return "red";
        case hal::Lamp::Blue: return "blue";
        case hal::Lamp::None:
        default: return "dark";
    }
}

Condition condition_for(const Situation& situation) {
    // Read down the priority order in kTable. Nothing below a matching line is
    // consulted, which is what makes the order the whole of the rule.
    if (!situation.running) return Condition::Off;
    if (situation.alarm_level >= kAlarmTakesLamp) return Condition::Alarm;
    if (situation.external_power)
        return situation.charge_complete ? Condition::Charged : Condition::Charging;
    if (situation.power_level == power::PowerLevel::Low ||
        situation.power_level == power::PowerLevel::Cutoff)
        return Condition::Low;
    return situation.fix_valid ? Condition::Alive : Condition::NoFix;
}

Command Policy::update(const Situation& situation, uint32_t now_ms) {
    const Condition wanted = condition_for(situation);
    if (wanted != condition_ || !started_) {
        condition_ = wanted;
        started_ = true;
        phase_ms_ = now_ms;
        // A condition that has just changed shows itself immediately: the flash a
        // pilot is owed is the one that follows the event, not the one that
        // follows the rest of the previous cycle.
        lit_ = indication_for(condition_).lamp != hal::Lamp::None;
    } else {
        advance(now_ms);
    }

    Command command{};
    command.lamp = lit_ ? indication_for(condition_).lamp : hal::Lamp::None;
    command.changed = command.lamp != shown_;
    shown_ = command.lamp;
    return command;
}

void Policy::advance(uint32_t now_ms) {
    const Indication& indication = indication_for(condition_);
    if (indication.lamp == hal::Lamp::None || indication.on_ms == 0) {
        lit_ = false;
        return;
    }
    // Held: no cadence to advance, and no arithmetic that could ever turn it off.
    if (indication.off_ms == 0) {
        lit_ = true;
        return;
    }

    // Unsigned subtraction throughout, so the 49.7-day wrap of the millisecond
    // counter costs at most one restarted flash rather than a lamp stuck in the
    // phase it was in when the counter turned over.
    const uint32_t cycle = static_cast<uint32_t>(indication.on_ms) + indication.off_ms;
    const uint32_t elapsed = now_ms - phase_ms_;
    // A pass that arrives a whole cycle or more late restarts the flash instead of
    // unwinding the phases it missed: nobody saw them, and replaying them would
    // spend the loop catching up with a blink.
    if (elapsed >= cycle) {
        phase_ms_ = now_ms;
        lit_ = true;
        return;
    }
    const uint16_t phase_ms = lit_ ? indication.on_ms : indication.off_ms;
    if (elapsed < phase_ms) return;
    phase_ms_ += phase_ms;
    lit_ = !lit_;
}

}  // namespace skyblip::indication
