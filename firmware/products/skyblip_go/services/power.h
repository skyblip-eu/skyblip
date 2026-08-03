#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H

#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "runtime/service.h"

namespace skyblip::go {

// Owns state.battery and state.power_level: what the divider read becomes the
// voltage and the state of charge every screen and the companion link report,
// and what core/power's cutoff rule made of the same samples. The level is
// published rather than re-derived downstream, so the one place that knows the
// cell is nearly gone is the one place that says so.
class PowerService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

    bool cutoff() const { return cutoff_.cutoff(); }
    uint32_t implausible_samples() const { return cutoff_.implausible(); }

   private:
    power::Gauge gauge_{};
    power::CutoffMonitor cutoff_{};
};

}  // namespace skyblip::go

#endif
