#include "core/power/wake.h"

namespace skyblip::power {

const char* to_string(BootPath path) {
    switch (path) {
        case BootPath::SleepAgain: return "SLEEP AGAIN";
        case BootPath::Run: break;
    }
    return "RUN";
}

namespace {

bool too_flat_to_run(const BootCell& cell) {
    if (!cell.valid || cell.external_power) return false;
    if (cell.millivolts <= kImplausibleFloorMv) return false;
    return cell.millivolts < kBootLockoutMv;
}

}  // namespace

ButtonWake button_wake_after_refusal(const BootCell& cell) {
    return too_flat_to_run(cell) ? ButtonWake::Withheld : ButtonWake::Armed;
}

BootPath boot_path(ResetCause causes, bool button_down, const BootCell& cell) {
    if (too_flat_to_run(cell)) return BootPath::SleepAgain;
    if (button_down) return BootPath::Run;
    if (has_cause(causes, ResetCause::Pin)) return BootPath::Run;
    // Both, not either. VBUS alone is not enough to refuse a boot: the bit is
    // only ever set on a wake from SYSTEM OFF, and if a future silicon or a
    // future adapter reported it any other way, refusing on it alone would be a
    // device that will not switch on.
    if (!has_cause(causes, ResetCause::LowPowerWake)) return BootPath::Run;
    if (!has_cause(causes, ResetCause::UsbVbus)) return BootPath::Run;
    return BootPath::SleepAgain;
}

}  // namespace skyblip::power
