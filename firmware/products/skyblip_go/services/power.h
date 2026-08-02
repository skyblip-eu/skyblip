#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H

#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "runtime/service.h"

namespace skyblip::go {

// Owns state.battery: what the divider read becomes the voltage and the state of
// charge every screen and the companion link report. It also runs the sample
// stream past core/power's cutoff rule, so the one place that knows the cell is
// nearly gone is the one place that says so.
class PowerService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

    power::PowerLevel level() const { return cutoff_.level(); }
    bool warning() const { return cutoff_.level() == power::PowerLevel::Low; }
    bool cutoff() const { return cutoff_.cutoff(); }
    uint32_t implausible_samples() const { return cutoff_.implausible(); }

   private:
    power::Gauge gauge_{};
    power::CutoffMonitor cutoff_{};
};

}  // namespace skyblip::go

#endif
