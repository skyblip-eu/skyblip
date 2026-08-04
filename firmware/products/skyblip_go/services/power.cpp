#include "products/skyblip_go/services/power.h"

namespace skyblip::go {

// Gated on the capability and not on the port's own answer, the same way the
// board gates the barometer and the divider it polls: a part the platform never
// found is a part nobody asks. The port would answer false anyway - this is what
// keeps a device that has no sensor from calling into a driver once every ten
// seconds to be told so.
void PowerService::sample_die_temperature(uint32_t now_ms) {
    if (!hal::has(context_.roles.capabilities, hal::Capability::DieTemperature)) return;
    // Unsigned subtraction, so the 49.7-day wrap of the millisecond counter costs
    // one late reading and not a service that never reads again.
    if (die_sampled_ && now_ms - die_read_ms_ < kDiePeriodMs) return;
    die_sampled_ = true;
    die_read_ms_ = now_ms;
    int16_t decicelsius = 0;
    // A refused measurement leaves the last good one standing rather than
    // publishing a zero: the reading is minutes old by design anyway.
    if (!die_->read(decicelsius)) return;
    die_dc_ = decicelsius;
    die_valid_ = true;
}

void PowerService::tick(uint32_t now_ms) {
    messages::BatterySample raw{};
    while (context_.bus.battery.pop(raw)) {
        // Trimmed once, here, on the way out of the queue: the gauge and the
        // cutoff monitor are separate objects fed from the same stream, and a
        // cutoff that fired 40 mV early on a trimmed unit would be the
        // calibration causing the failure it exists to prevent. See
        // core/power/battery.h for what the offset is and where it comes from.
        const messages::BatterySample sample =
            power::calibrated(raw, context_.state.settings.battery_offset_mv);
        gauge_.apply(sample);
        cutoff_.apply(sample);
    }
    context_.state.battery = gauge_.state();
    context_.state.power_level = cutoff_.level();
    sample_die_temperature(now_ms);
}

}  // namespace skyblip::go
