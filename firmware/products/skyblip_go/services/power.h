#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_POWER_H

#include "core/power/battery.h"
#include "runtime/service.h"

namespace skyblip::go {

// Owns state.battery: what the divider read becomes the voltage and the state of
// charge every screen and the companion link report.
class PowerService : public runtime::Service {
   public:
    using runtime::Service::Service;

    void tick(uint32_t now_ms) override;

   private:
    power::Gauge gauge_{};
};

}  // namespace skyblip::go

#endif
