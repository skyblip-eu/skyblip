// core/flight/atmosphere.h — the standard atmosphere, as integer math.
//
// Pressure is the only altitude source that is datum-free in a useful way: a
// RATE derived from it needs no agreement with anyone. That is what this is for
// — vertical speed. It is deliberately NOT used for the altitude skyBlip
// broadcasts, because the collision alarm compares RELATIVE altitude against
// other aircraft (core/traffic/alarm.h, +/-300 m window), so every participant
// must share one datum, and which datum that is belongs to the ADS-L spec, not
// to this file.
#ifndef SKYBLIP_CORE_FLIGHT_ATMOSPHERE_H
#define SKYBLIP_CORE_FLIGHT_ATMOSPHERE_H

#include <cstdint>

namespace skyblip::flight {

// ISA sea-level pressure.
constexpr uint32_t kIsaSeaLevelPa = 101325;

// Pressure -> ISA pressure altitude, in CENTIMETRES. Centimetres, not metres,
// because vertical speed is a difference of two of these a second or two apart:
// at 1 m resolution a 0.5 m/s climb would quantise to nothing.
//
// Clamped to the table's range (26000..110000 Pa, about -698..+10108 m), which
// also covers the ISA troposphere limit — the formula below is only valid to
// 11 km (22632 Pa) anyway.
int32_t pressure_to_alt_cm(uint32_t pa);

// The inverse, by bisection over the SAME table, so a caller that needs to go
// the other way (a modelled barometer, a test) cannot drift from the forward
// curve. Slower, and never on the flight path.
uint32_t alt_cm_to_pressure(int32_t alt_cm);

// Altitude above the datum an altimeter subscale names: pressure altitude of
// the air outside, less the pressure altitude of the setting. QNH in gives
// altitude above mean sea level, 1013.25 hPa in gives pressure altitude, and a
// field's QFE gives height above that field.
int32_t alt_cm_on_setting(uint32_t pa, uint32_t setting_pa);

// Shortest window that still averages out sensor noise, and the longest one
// whose answer is still "now" rather than a history lesson.
constexpr uint32_t kMinWindowMs = 500;
constexpr uint32_t kMaxWindowMs = 10000;

// Vertical speed in eighth-metres per second (the ADS-L climb unit) from two
// pressure altitudes and the time between them. Returns false when dt is too
// short to divide by, or so long the answer would be meaningless.
bool climb_e8_from_alt(int32_t alt_cm_now, int32_t alt_cm_then, uint32_t dt_ms, int16_t& out_e8);

}  // namespace skyblip::flight

#endif
