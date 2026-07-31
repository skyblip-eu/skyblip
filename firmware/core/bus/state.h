#ifndef SKYBLIP_CORE_BUS_STATE_H
#define SKYBLIP_CORE_BUS_STATE_H

#include "core/flight/atmosphere.h"
#include "core/messages/messages.h"
#include "core/power/battery.h"
#include "core/settings/settings.h"
#include "core/timing/slot.h"
#include "core/traffic/table.h"

namespace skyblip::bus {

struct State {
    settings::Settings settings{};
    messages::OwnState own{};
    timing::ClockState clock{};
    timing::SlotPlan plan{};
    traffic::TrafficTable traffic{};
    power::BatteryState battery{};

    uint8_t alarm_level{0};
    uint32_t rx_ok{0};
    uint32_t rx_bad{0};
    uint32_t tx_ok{0};
    uint32_t tx_busy{0};
    uint32_t gnss_fixes{0};
    int16_t turn_dps{0};
    uint32_t pressure_pa{0};
    // The altimeter subscale, as the pilot sets it: standard until told otherwise.
    uint32_t qnh_pa{flight::kIsaSeaLevelPa};
    bool baro_active{false};
    bool started{false};

    // The traffic table's single time base. Mixing GNSS epoch seconds with
    // boot-relative seconds underflows uint32 and ages every target out at once.
    uint32_t traffic_now(uint32_t now_ms) const { return own.utc_valid ? own.utc : now_ms / 1000; }
};

}  // namespace skyblip::bus

#endif
