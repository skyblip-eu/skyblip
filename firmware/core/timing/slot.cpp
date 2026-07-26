#include "core/timing/slot.h"

namespace skyblip::timing {

SlotState Scheduler::state_at(int phase_ms) {
    if (phase_ms >= kUplinkRxEnd && phase_ms < kSlot0Start) return SlotState::SwitchOtoM;
    if (phase_ms >= kSlot0Start && phase_ms <= kSlot0End) return SlotState::Slot0;
    if (phase_ms > kSlot0End && phase_ms < kSlot1Start) return SlotState::Hop;
    if (phase_ms >= kSlot1Start && phase_ms < kSlot1End) return SlotState::Slot1;
    if (phase_ms >= kUplinkRxStart && phase_ms < kUplinkRxEnd) return SlotState::UplinkRxO;
    return SlotState::SwitchMtoO;
}

Band Scheduler::band_at(int phase_ms) {
    SlotState s = state_at(phase_ms);
    return (s == SlotState::UplinkRxO || s == SlotState::SwitchMtoO) ? Band::O : Band::M;
}

SlotPlan Scheduler::plan(int phase_ms, const ClockState& clock) const {
    if (phase_ms < 0) phase_ms = 0;
    if (phase_ms >= 1000) phase_ms %= 1000;

    SlotPlan p{};
    p.state = state_at(phase_ms);
    p.band = band_at(phase_ms);
    p.own_tx_slot = in_own_tx_slot(phase_ms);

    bool anchored = clock.utc_valid && clock.pps_locked;
    bool within_holdover =
        clock.utc_valid && !clock.pps_locked && clock.ms_since_pps <= kPpsHoldoverMs;

    if (anchored) {
        p.listen_only = false;
        p.tx_allowed = (p.band == Band::M) && p.own_tx_slot;
    } else if (within_holdover) {
        p.listen_only = false;
        p.tx_allowed = false;
    } else {
        p.listen_only = true;
        p.tx_allowed = false;
    }
    return p;
}

}
