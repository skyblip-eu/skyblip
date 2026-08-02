#include "products/skyblip_go/services/power.h"

namespace skyblip::go {

void PowerService::tick(uint32_t) {
    messages::BatterySample sample{};
    while (context_.bus.battery.pop(sample)) {
        gauge_.apply(sample);
        cutoff_.apply(sample);
    }
    context_.state.battery = gauge_.state();
    context_.state.power_level = cutoff_.level();
}

}  // namespace skyblip::go
