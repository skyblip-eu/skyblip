// core/timing/slot.h — the single-radio duty slot map (3-ARCHITECTURE §0), the
#ifndef SKYBLIP_CORE_TIMING_SLOT_H
#define SKYBLIP_CORE_TIMING_SLOT_H

#include <cstdint>

namespace skyblip::timing {

constexpr int kUplinkRxStart = 205;
constexpr int kUplinkRxEnd = 395;
constexpr int kSlot0Start = 400;
constexpr int kSlot0End = 799;
constexpr int kSlot1Start = 800;
constexpr int kSlot1End = 1000;
constexpr int kJitterGuardMs = 5;
constexpr int kHopGuardMs = 1;

constexpr int kGroundEmitStart = 210;
constexpr int kGroundEmitEnd = 390;

constexpr uint32_t kPpsHoldoverMs = 60000;

enum class Band : uint8_t { M, O };

enum class SlotState : uint8_t {
    UplinkRxO,
    SwitchOtoM,
    Slot0,
    Hop,
    Slot1,
    SwitchMtoO,
};

struct ClockState {
    bool utc_valid{false};
    bool pps_locked{false};
    uint32_t ms_since_pps{0};
};

struct SlotPlan {
    SlotState state;
    Band band;
    bool tx_allowed;
    bool own_tx_slot;
    bool listen_only;
};

class Scheduler {
   public:
    SlotPlan plan(int phase_ms, const ClockState& clock) const;

    static SlotState state_at(int phase_ms);
    static Band band_at(int phase_ms);
    static bool in_uplink_rx(int phase_ms) {
        return phase_ms >= kUplinkRxStart && phase_ms < kUplinkRxEnd;
    }
    static bool in_own_tx_slot(int phase_ms) {
        return (phase_ms >= kSlot0Start && phase_ms <= kSlot0End) ||
               (phase_ms >= kSlot1Start && phase_ms < kSlot1End);
    }
};

}

#endif
