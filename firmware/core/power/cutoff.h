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

// What a durable write is for. Only two kinds exist and the difference between
// them is the whole rule below: one is a convenience a pilot can make again, the
// other is the evidence of the flight that is ending.
enum class DurableWrite : uint8_t {
    // The settings blob, on the internal NVS sector. Rewritten on every accepted
    // change, and the sector NVS garbage-collects is the same internal flash the
    // running image executes from.
    Settings,
    // The flight log record. Losing it loses the flight, and the moment it is
    // most needed is the moment the cell is going: a landing out, a pack that
    // sagged under a burst, a device switched off in a hurry.
    FlightRecord,
};

// THE RULE. Below the warning level nothing durable is written except the record
// that must survive.
//
// It is one function and not a second threshold on purpose: the levels are the
// ones the monitor below already publishes from kLowWarnMv, kCutoffMv and the
// implausible floor, so "stop writing" happens at exactly the voltage the panel
// and the tablet already say LOW at, and a unit whose divider is unpopulated
// (Unknown, the floor caught every sample) is not a unit whose settings screen
// stops working.
//
// A latched supply warning outranks the voltage entirely: POFCON compares the
// SoC's own rail, so once it has fired, what the divider says about the cell is
// no longer the question.
bool may_write(PowerLevel level, bool supply_warned, DurableWrite kind);

// Fed the same sample stream the gauge sees. Latches at Cutoff: once the device
// has decided to go down, a cell that bounces back up on the relief of the radio
// going quiet must not cancel it.
class CutoffMonitor {
   public:
    PowerLevel apply(const messages::BatterySample& sample);

    PowerLevel level() const { return level_; }
    bool warned() const { return level_ == PowerLevel::Low || level_ == PowerLevel::Cutoff; }
    bool cutoff() const { return level_ == PowerLevel::Cutoff; }

    // The nRF52840's power-failure comparator fired (POFCON, POFWARN). Latching:
    // the rail crossed a threshold it has no business crossing, and a rail that
    // came back up does not make that un-happen.
    //
    // INFO: fc 05aug26 It deliberately does NOT drive the level to Cutoff, so it
    // does not start a shutdown. POFCON warns about VDD, well below any healthy
    // cell, so by the time it fires there is no orderly shutdown left to run -
    // the panel park alone is kParkMs = 3 s and the brownout reset is milliseconds
    // away. The one useful thing at that moment is to not be inside a write, and
    // that is exactly what this does. The voltage rule keeps the shutdown, where
    // it has three consecutive samples of evidence to act on.
    void on_supply_warning();
    bool supply_warned() const { return supply_warned_; }
    // How many times it fired. A unit that reports any at all on a healthy cell
    // has a supply problem, or a threshold set too high, and both are bench work.
    uint32_t supply_warnings() const { return supply_warnings_; }

    // The rule above, asked of what this monitor currently knows.
    bool may_write(DurableWrite kind) const {
        return power::may_write(level_, supply_warned_, kind);
    }

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
    uint32_t supply_warnings_{0};
    bool supply_warned_{false};
};

}  // namespace skyblip::power

#endif
