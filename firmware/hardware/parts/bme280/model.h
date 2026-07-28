#ifndef SKYBLIP_HARDWARE_MODEL_BME280_H
#define SKYBLIP_HARDWARE_MODEL_BME280_H

#include "core/flight/atmosphere.h"

namespace skyblip::models {

class Bme280 {
   public:
    void set_altitude_m(int32_t m) { pressure_pa_ = flight::alt_cm_to_pressure(m * 100); }

    void set_pressure_pa(uint32_t pa) { pressure_pa_ = pa; }

    uint32_t pressure_pa() const { return pressure_pa_; }

   private:
    uint32_t pressure_pa_{flight::kIsaSeaLevelPa};
};

}  // namespace skyblip::models

#endif
