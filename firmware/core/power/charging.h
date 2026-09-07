#ifndef SKYBLIP_CORE_POWER_CHARGING_H
#define SKYBLIP_CORE_POWER_CHARGING_H

#include <cstdint>

namespace skyblip::power {

enum class ChargeCondition : uint8_t { Unknown, Ok, TooCold, TooHot };

const char* to_string(ChargeCondition condition);

// INFO: fc 07sep26 LilyGO's 0-60 C window, project/research/enclosure-and-mount-go.md:61
constexpr int16_t kChargeColdDeciCelsius = 50;
constexpr int16_t kChargeHotDeciCelsius = 600;

static_assert(kChargeColdDeciCelsius > 0,
              "the die reads above ambient, so a 0 C limit read on it is already too cold");

ChargeCondition charge_condition(bool external_power, bool temperature_valid, int16_t decicelsius);

}  // namespace skyblip::power

#endif
