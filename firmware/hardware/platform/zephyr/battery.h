#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BATTERY_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BATTERY_H
#if defined(__ZEPHYR__)

#include <hal/nrf_power.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <cstdint>

#include "core/power/cutoff.h"

namespace skyblip::platform::zephyr {

// The cell, read through the board's divider. Zephyr's voltage-divider driver
// owns the ADC and the ratio (the resistors are a devicetree fact, not a
// constant in code). We own what the millivolts mean, in core/power.
//
// INFO: fc 09mar26 the T-Echo family exposes no charger status pin - SoftRF's
// nRF52 port says the same ("the T-Echo has no ext_power_pin") - so the only
// charge signal is the USB regulator's VBUS detect. Nothing here can measure
// charge current, which is why "charging" ends at a voltage plateau.
class Battery {
   public:
    explicit Battery(const struct device* dev) : dev_(dev) {}

    bool ready() const { return device_is_ready(dev_); }

    bool read_mv(uint16_t& out_mv) {
        if (!ready()) return false;
        if (sensor_sample_fetch(dev_) != 0) return false;
        struct sensor_value volts{};
        if (sensor_channel_get(dev_, SENSOR_CHAN_VOLTAGE, &volts) != 0) return false;

        const int32_t millivolts = volts.val1 * 1000 + volts.val2 / 1000;
        if (millivolts < kPlausibleMinMv || millivolts > kPlausibleMaxMv) return false;
        out_mv = static_cast<uint16_t>(millivolts);
        return true;
    }

    static bool external_power() { return nrf_power_usbregstatus_vbusdet_get(NRF_POWER); }

   private:
    // INFO: fc 07sep26 nothing here charges past the 4.2 V float, the low end is core/power's
    static constexpr int32_t kPlausibleMinMv = power::kImplausibleFloorMv;
    static constexpr int32_t kPlausibleMaxMv = 4700;

    const struct device* dev_;
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
