// devices/models/baro.h — a model of the barometer at the PRESSURE seam, not at
// the chip's I2C seam.
//
// Deliberate, and the reason is recorded in 3-ARCHITECTURE §8: the BME280 is a
// plain I2C sensor Zephyr already drives, so re-transcribing Bosch's
// compensation formulas would buy a host test whose only oracle is the same
// datasheet the driver was written from — the common-mode error the doc warns
// about. So the framework owns the chip, and the seam we model is the value.
//
// Everything above this line IS ours and IS tested: the ISA conversion
// (core/flight/atmosphere), the vertical-speed derivation, and the screens.
#ifndef SKYBLIP_DEVICES_MODELS_BARO_H
#define SKYBLIP_DEVICES_MODELS_BARO_H

#include "core/flight/atmosphere.h"

namespace skyblip::models {

class Baro {
   public:
    // Drive the model by altitude, which is what a caller actually wants to say.
    // Inverted through the SAME table the flight code reads forward, so the model
    // cannot disagree with the maths under test.
    void set_altitude_m(int32_t m) { pressure_pa_ = flight::alt_cm_to_pressure(m * 100); }

    // Or set the pressure directly, for a sensor reading straight off a log.
    void set_pressure_pa(uint32_t pa) { pressure_pa_ = pa; }

    uint32_t pressure_pa() const { return pressure_pa_; }

   private:
    uint32_t pressure_pa_{flight::kIsaSeaLevelPa};
};

}  // namespace skyblip::models

#endif
