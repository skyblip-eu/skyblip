#include "core/power/shutdown.h"

namespace skyblip::power {

const char* to_string(ShutdownReason reason) {
    switch (reason) {
        case ShutdownReason::LongPress: return "BUTTON";
        case ShutdownReason::Stow: return "STOW";
        case ShutdownReason::LowBattery: return "LOW BATTERY";
        case ShutdownReason::LinkRequest: return "LINK";
        case ShutdownReason::Install: return "INSTALL";
        case ShutdownReason::None: break;
    }
    return "NONE";
}

const char* to_string(PowerDownStep step) {
    switch (step) {
        case PowerDownStep::RadioSleep: return "RADIO SLEEP";
        case PowerDownStep::ExternalFlashDeepPowerDown: return "FLASH DPD";
        case PowerDownStep::ExternalFlashLinesReleased: return "FLASH LINES";
        case PowerDownStep::GnssBackupOff: return "GNSS OFF";
        case PowerDownStep::GnssResetAsserted: return "GNSS RESET";
        case PowerDownStep::RadioResetAsserted: return "RADIO RESET";
        case PowerDownStep::PeripheralRailOff: return "RAIL OFF";
        case PowerDownStep::AuxRailOff: return "AUX RAIL OFF";
        case PowerDownStep::DrivenPinsReleased: return "PINS RELEASED";
        case PowerDownStep::WakePinArmed: return "WAKE ARMED";
    }
    return "NONE";
}

ButtonWake button_wake_after(ShutdownReason reason, bool external_power) {
    if (external_power) return ButtonWake::Armed;
    return reason == ShutdownReason::LowBattery ? ButtonWake::Withheld : ButtonWake::Armed;
}

void power_down(PowerDownSink& sink, ButtonWake button_wake) {
    for (int i = 0; i < kPowerDownStepCount; i++) {
        if (kPowerDownOrder[i] == PowerDownStep::WakePinArmed &&
            button_wake == ButtonWake::Withheld)
            continue;
        sink.perform(kPowerDownOrder[i]);
    }
}

void ShutdownSequencer::enter(ShutdownPhase phase, uint32_t now_ms) {
    phase_ = phase;
    since_ms_ = now_ms;
}

void ShutdownSequencer::request(ShutdownReason reason, uint32_t now_ms) {
    if (going_down() || reason == ShutdownReason::None) return;
    reason_ = reason;
    released_ = false;
    enter(ShutdownPhase::Parking, now_ms);
}

uint32_t ShutdownSequencer::held_ms(uint32_t now_ms) const {
    if (!holding_ || !hold_armed_) return 0;
    return now_ms - hold_since_ms_;
}

void ShutdownSequencer::tick(uint32_t now_ms, bool button_down, bool pad_down) {
    switch (phase_) {
        case ShutdownPhase::Running:
            if (!button_down) {
                holding_ = false;
                hold_armed_ = true;
                return;
            }
            if (!holding_) {
                holding_ = true;
                hold_since_ms_ = now_ms;
                stowing_ = pad_down;
                return;
            }
            if (!pad_down) stowing_ = false;
            if (hold_armed_ && now_ms - hold_since_ms_ >= kLongPressMs)
                request(stowing_ ? ShutdownReason::Stow : ShutdownReason::LongPress, now_ms);
            return;

        case ShutdownPhase::Parking:
            if (now_ms - since_ms_ < kParkMs) return;
            enter(waits_for_release() ? ShutdownPhase::AwaitRelease : ShutdownPhase::Off, now_ms);
            return;

        case ShutdownPhase::AwaitRelease:
            if (button_down) {
                released_ = false;
                return;
            }
            if (!released_) {
                released_ = true;
                released_at_ms_ = now_ms;
                return;
            }
            if (now_ms - released_at_ms_ >= kReleaseSettleMs) enter(ShutdownPhase::Off, now_ms);
            return;

        case ShutdownPhase::Off: return;
    }
}

}  // namespace skyblip::power
