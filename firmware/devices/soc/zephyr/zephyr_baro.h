// devices/soc/zephyr/zephyr_baro.h — the BME280 through Zephyr's sensor API.
//
// No first-party BME280 driver: the chip is a plain I2C sensor the framework
// already drives, and transcribing Bosch's compensation formulas would give us a
// host test whose only oracle is the datasheet the driver came from
// (3-ARCHITECTURE §8, common-mode error). What IS ours starts one level up, at
// the pressure value: core/flight/atmosphere and the vertical-speed derivation,
// both host-tested against the ICAO closed form.
//
// Reports PASCALS. Zephyr's pressure channel is kilopascals as a
// sensor_value{val1 = kPa, val2 = micro-kPa}, which is a trap: reading only
// val1 quantises to 1 kPa, i.e. about 80 m of altitude.
#ifndef SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_BARO_H
#define SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_BARO_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include "core/util/result.h"

namespace skyblip::soc::zephyr {

class ZephyrBaro {
   public:
    explicit ZephyrBaro(const struct device* dev) : dev_(dev) {}

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

}  // namespace skyblip::soc::zephyr
#endif  // __ZEPHYR__
#endif
