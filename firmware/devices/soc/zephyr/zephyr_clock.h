// devices/soc/zephyr/zephyr_clock.h — hal::Clock over the Zephyr kernel clock.
#ifndef SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_CLOCK_H
#define SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_CLOCK_H
#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

#include "hal/clock.h"

namespace skyblip::soc::zephyr {

class ZephyrClock : public hal::Clock {
   public:
    uint32_t millis() const override { return static_cast<uint32_t>(k_uptime_get()); }
    uint64_t micros() const override { return k_ticks_to_us_floor64(k_uptime_ticks()); }
};

}  // namespace skyblip::soc::zephyr
#endif  // __ZEPHYR__
#endif
