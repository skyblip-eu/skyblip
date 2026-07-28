#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_CLOCK_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_CLOCK_H
#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

#include "hal/clock.h"

namespace skyblip::platform::zephyr {

class Clock : public hal::Clock {
   public:
    uint32_t millis() const override { return static_cast<uint32_t>(k_uptime_get()); }
    uint64_t micros() const override { return k_ticks_to_us_floor64(k_uptime_ticks()); }
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
