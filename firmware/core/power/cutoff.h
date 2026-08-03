// core/power/cutoff.h: when a falling cell stops being a reading and becomes an
// action. The read side (is this millivolt figure believable at all) lives in the
// battery adapter; this is the acting half.
#ifndef SKYBLIP_CORE_POWER_CUTOFF_H
#define SKYBLIP_CORE_POWER_CUTOFF_H

#include <cstdint>

#include "core/messages/messages.h"

namespace skyblip::power {

// A single Li-ion cell. The warning is where a pilot still has time to land, the
// cutoff is where the pack is close enough to its protection circuit that a
// 22 dBm burst can trip it and take the log and the settings write with it.
constexpr uint16_t kLowWarnMv = 3500;
constexpr uint16_t kCutoffMv = 3200;

// INFO: hk 02aug26 an unpopulated or unconnected divider reads as a slow drift
// near zero, not as a flat cell. SoftRF calls the same floor
// BATTERY_THRESHOLD_INVALID (src/driver/Battery.h:24) and refuses to act below
// it, because a floating ADC that can power the device off is worse than no
// gauge at all.
constexpr uint16_t kImplausibleFloorMv = 1800;

// More than two consecutive samples, so the third one acts. A transmit burst
// sags the rail for as long as it lasts, and the sampler is slower than a burst,
// but a charger unplugged mid-sample is not.
constexpr uint8_t kConsecutiveSamples = 3;

enum class PowerLevel : uint8_t { Unknown, Normal, Low, Cutoff };

const char* to_string(PowerLevel level);

// Fed the same sample stream the gauge sees. Latches at Cutoff: once the device
// has decided to go down, a cell that bounces back up on the relief of the radio
// going quiet must not cancel it.
class CutoffMonitor {
   public:
    PowerLevel apply(const messages::BatterySample& sample);

    PowerLevel level() const { return level_; }
    bool warned() const { return level_ == PowerLevel::Low || level_ == PowerLevel::Cutoff; }
    bool cutoff() const { return level_ == PowerLevel::Cutoff; }

    uint8_t below_cutoff() const { return below_cutoff_; }
    uint8_t below_warn() const { return below_warn_; }
    // Readings the sanity floor threw away. A gauge that never rises above zero
    // here is a gauge nobody should trust.
    uint32_t implausible() const { return implausible_; }

   private:
    PowerLevel level_{PowerLevel::Unknown};
    uint8_t below_cutoff_{0};
    uint8_t below_warn_{0};
    uint32_t implausible_{0};
};

}  // namespace skyblip::power

#endif
