// core/power/battery.h: what a cell's terminal voltage means, on charge and off.
#ifndef SKYBLIP_CORE_POWER_BATTERY_H
#define SKYBLIP_CORE_POWER_BATTERY_H

#include <cstdint>

#include "core/messages/messages.h"

namespace skyblip::power {

constexpr uint16_t kEmptyMv = 3300;
constexpr uint16_t kFullMv = 4200;
// With external power present and the cell above this, the charger has finished:
// it is holding the float voltage, not pushing current in. There is no charge
// status pin to ask, so the plateau is the signal.
constexpr uint16_t kChargeCompleteMv = 4190;

// One reading, one meaning. Charging is not a modifier on a percentage, it is a
// different curve: a charger holds the terminal above the cell's open-circuit
// voltage by the drop across its internal resistance, so 4.00 V on charge is a
// cell far emptier than 4.00 V on the bench.
uint8_t percent_from_mv(uint16_t millivolts, bool charging);

struct BatteryState {
    uint16_t millivolts{0};
    uint8_t percent{0};
    bool external_power{false};
    bool charging{false};
    bool valid{false};
};

// Readings become something a pilot can read. A 22 dBm burst sags the rail for
// exactly as long as it lasts, so the gauge takes the median of the last three
// readings rather than an average: a transient is discarded whole, while a cell
// that is really moving is followed within two samples. And a gauge that walks
// backwards while the cable is in is a bug report, so the percentage only moves
// the way the current flows until the direction itself changes.
class Gauge {
   public:
    void apply(const messages::BatterySample& sample);

    const BatteryState& state() const { return state_; }

   private:
    static constexpr int kWindow = 3;

    uint16_t recent_[kWindow]{};
    int seen_{0};
    BatteryState state_{};
};

}  // namespace skyblip::power

#endif
