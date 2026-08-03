#ifndef SKYBLIP_CORE_BUS_STATE_H
#define SKYBLIP_CORE_BUS_STATE_H

#include "core/flight/atmosphere.h"
#include "core/messages/messages.h"
#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "core/settings/settings.h"
#include "core/timing/channel.h"
#include "core/timing/durable_write.h"
#include "core/timing/slot.h"
#include "core/timing/timing_stats.h"
#include "core/traffic/table.h"

namespace skyblip::bus {

struct State {
    settings::Settings settings{};
    messages::OwnState own{};
    timing::ClockState clock{};
    timing::SlotPlan plan{};
    // Where the radio believes it is inside the second it is arming, stamped with
    // the pass it said so on. The radio service is the only writer; whoever needs
    // to know whether the core may be stalled reads it here rather than deriving
    // the phase a second time (core/timing/durable_write.h).
    timing::DwellPhase dwell{};
    // The bench accumulator G6 reads out: hardware/boards is the one writer of
    // the PPS half, products/skyblip_go/services/radio.cpp of the dwell half.
    timing::SlotTimingStats timing_stats{};
    traffic::TrafficTable traffic{};
    power::BatteryState battery{};
    // What the cutoff monitor made of the same samples the gauge saw. Whoever
    // draws a low cell reads this rather than comparing millivolts again: the
    // debounce, the charger and the sanity floor are decided once.
    power::PowerLevel power_level{power::PowerLevel::Unknown};

    uint8_t alarm_level{0};
    // INFO: fc 03aug26 The carrier-sense threshold the next dwell will carry,
    // published because EN 300 220-2 V3.3.1 §4.6.2.3 evidence has to be readable
    // on a bench. The radio service is the only writer; before it has run this
    // is the cold-start threshold, which is what the dwell would carry too.
    int8_t carrier_sense_dbm{timing::NoiseFloor::kSeedDbm + timing::NoiseFloor::kClearMarginDb};

    uint32_t rx_ok{0};
    uint32_t rx_bad{0};
    uint32_t tx_ok{0};
    uint32_t tx_busy{0};
    // The instant the executor actually reported completion for, published by
    // whoever already drains messages::RfEvent (TrafficService) so the policy
    // layer that owns the deadline (RadioService) can measure against it
    // without a second reader of the bus.
    uint64_t last_tx_done_at_us{0};
    uint32_t gnss_fixes{0};
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
