#include "core/power/cutoff.h"

namespace skyblip::power {

const char* to_string(PowerLevel level) {
    switch (level) {
        case PowerLevel::Normal: return "OK";
        case PowerLevel::Low: return "LOW";
        case PowerLevel::Cutoff: return "CUTOFF";
        case PowerLevel::Unknown: break;
    }
    return "--";
}

PowerLevel CutoffMonitor::apply(const messages::BatterySample& sample) {
    if (level_ == PowerLevel::Cutoff) return level_;

    if (sample.millivolts <= kImplausibleFloorMv) {
        implausible_++;
        below_cutoff_ = 0;
        below_warn_ = 0;
        return level_;
    }

    // On the cable the terminal is held above the cell by the charge current, so
    // the number says nothing about what is left in it and nothing may act on it.
    if (sample.external_power) {
        below_cutoff_ = 0;
        below_warn_ = 0;
        level_ = PowerLevel::Normal;
        return level_;
    }

    below_cutoff_ = sample.millivolts < kCutoffMv ? static_cast<uint8_t>(below_cutoff_ + 1) : 0;
    below_warn_ = sample.millivolts < kLowWarnMv ? static_cast<uint8_t>(below_warn_ + 1) : 0;

    if (below_cutoff_ >= kConsecutiveSamples)
        level_ = PowerLevel::Cutoff;
    else if (below_warn_ >= kConsecutiveSamples)
        level_ = PowerLevel::Low;
    else if (below_warn_ == 0)
        level_ = PowerLevel::Normal;

    return level_;
}

}  // namespace skyblip::power
