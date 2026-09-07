#include "core/power/charging.h"

namespace skyblip::power {

const char* to_string(ChargeCondition condition) {
    switch (condition) {
        case ChargeCondition::Ok: return "OK";
        case ChargeCondition::TooCold: return "COLD";
        case ChargeCondition::TooHot: return "HOT";
        case ChargeCondition::Unknown: break;
    }
    return "UNKNOWN";
}

ChargeCondition charge_condition(bool external_power, bool temperature_valid, int16_t decicelsius) {
    if (!external_power || !temperature_valid) return ChargeCondition::Unknown;
    if (decicelsius < kChargeColdDeciCelsius) return ChargeCondition::TooCold;
    if (decicelsius > kChargeHotDeciCelsius) return ChargeCondition::TooHot;
    return ChargeCondition::Ok;
}

}  // namespace skyblip::power
