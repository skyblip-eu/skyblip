#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BARO_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BARO_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include "core/util/result.h"

namespace skyblip::platform::zephyr {

class Baro {
   public:
    explicit Baro(const struct device* dev) : dev_(dev) {}

    bool ready() const { return device_is_ready(dev_); }

    // One sample. Returns false and leaves `out_pa` untouched on any failure, so
    // a sensor that stops answering degrades to "no baro" rather than to a
    // plausible-looking wrong altitude.
    bool read_pressure_pa(uint32_t& out_pa) {
        if (sensor_sample_fetch(dev_) != 0) return false;
        struct sensor_value press{};
        if (sensor_channel_get(dev_, SENSOR_CHAN_PRESS, &press) != 0) return false;
        // kPa -> Pa, keeping the fractional part: val2 is micro-kPa.
        const int64_t pa = static_cast<int64_t>(press.val1) * 1000 + press.val2 / 1000;
        if (pa < 1000 || pa > 200000) return false;  // implausible: treat as a fault
        out_pa = static_cast<uint32_t>(pa);
        return true;
    }

   private:
    const struct device* dev_;
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
