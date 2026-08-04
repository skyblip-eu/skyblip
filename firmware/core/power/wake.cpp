#include "core/power/wake.h"

namespace skyblip::power {

const char* to_string(BootPath path) {
    switch (path) {
        case BootPath::SleepAgain: return "SLEEP AGAIN";
        case BootPath::Run: break;
    }
    return "RUN";
}

BootPath boot_path(ResetCause causes, bool button_down) {
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
