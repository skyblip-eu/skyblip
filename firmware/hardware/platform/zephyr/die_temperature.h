#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DIE_TEMPERATURE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DIE_TEMPERATURE_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#include <cstdint>

#include "hal/die_temperature.h"

namespace skyblip::platform::zephyr {

// The nRF52840's own TEMP peripheral, through Zephyr's temp_nrf5 driver
// ("nordic,nrf-temp") rather than the register directly: on this SoC the radio
// stack also measures the die to calibrate the RC oscillator, and the driver is
// what serialises the two. A bare nrf_temp_task_trigger() from a service thread
// would be a second owner of a peripheral the BLE controller already uses.
//
// The measurement itself is one shot, tens of microseconds, and blocking: it is
// deliberately not read anywhere near a dwell. Nothing here is on the radio
// thread.
class DieTemperature : public hal::DieTemperature {
   public:
    bool ready() const { return dev_ != nullptr && device_is_ready(dev_); }

    bool read(int16_t& decicelsius) override {
        if (!ready()) return false;
        if (sensor_sample_fetch(dev_) != 0) return false;
        struct sensor_value temp{};
        if (sensor_channel_get(dev_, SENSOR_CHAN_DIE_TEMP, &temp) != 0) return false;
        // val1 is whole degrees, val2 micro-degrees, either of which may be
        // negative on a cold morning.
        const int32_t deci = temp.val1 * 10 + temp.val2 / 100000;
        // Outside this the sensor is broken rather than the device cold or hot:
        // the nRF52840 datasheet's own operating range is -40 to +105 C, and a
        // reading past it must not be published as a temperature.
        if (deci < -500 || deci > 1250) return false;
        decicelsius = static_cast<int16_t>(deci);
        return true;
    }

   private:
#if DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_temp)
    const struct device* dev_{DEVICE_DT_GET_ONE(nordic_nrf_temp)};
#else
    // A build without the sensor driver is a build with an absent capability, and
    // it still links: that is the whole point of the null answer in
    // hal/die_temperature.h.
    const struct device* dev_{nullptr};
#endif
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
