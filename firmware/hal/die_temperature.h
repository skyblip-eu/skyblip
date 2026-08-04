// hal/die_temperature.h: capability port: how warm the silicon is.
//
// The SX1262 has a temperature sensor and it is not worth reading: OGN says so
// outright and falls back to the nRF52 die sensor instead (nrf52-ogn-tracker
// src/proc.cpp:1112-1114). So this is the SoC's own sensor, and it answers one
// question that no other number on the device can: was this unit cooking. A
// canopy rail in August is 60 C of air over a black case, and the two failures
// that follow - a pack that will not charge and an e-paper panel that ghosts -
// both look like faults in the part that gave up rather than in the afternoon
// that did it. One line on silicon, and it is the line that closes a support
// case.
//
// Deci-celsius, because the nRF52 TEMP peripheral resolves 0.25 C and rounding a
// sensor's own resolution away at the port is a decision the port has no business
// making. Whole degrees is what a reply is free to print.
//
// A board with no sensor is an absent capability, not a null pointer and not a
// zero reading: read() answers false, hal::Capability says so once, and the
// reply simply has no key. This class IS the absent part - it is not abstract, so
// a platform without a sensor needs no second type to stand in for one.
#ifndef SKYBLIP_HAL_DIE_TEMPERATURE_H
#define SKYBLIP_HAL_DIE_TEMPERATURE_H

#include <cstdint>

namespace skyblip::hal {

class DieTemperature {
   public:
    virtual ~DieTemperature() = default;

    // Tenths of a degree Celsius. False leaves the caller's value untouched: the
    // sensor is absent, not ready, or refused this measurement, and a reading
    // nobody took must never be published as one that was.
    virtual bool read(int16_t& decicelsius) {
        (void)decicelsius;
        return false;
    }
};

}  // namespace skyblip::hal

#endif
