#include "core/timing/slot.h"

namespace skyblip::timing {

SlotState Scheduler::state_at(int phase_ms) {
    // The tail first: before anything else in a second, the radio is still in
    // the slot 1 that opened 800 ms into the previous one.
    if (phase_ms < kSlot1Wrap) return SlotState::Slot1;
    if (phase_ms >= kUplinkRxStart && phase_ms < kUplinkRxEnd) return SlotState::UplinkRxO;
    if (phase_ms >= kUplinkRxEnd && phase_ms < kSlot0Start) return SlotState::SwitchOtoM;
    if (phase_ms >= kSlot0Start && phase_ms < kSlot0End - kHopGuardMs) return SlotState::Slot0;
    if (phase_ms >= kSlot0End - kHopGuardMs && phase_ms < kSlot1Start) return SlotState::Hop;
    if (phase_ms >= kSlot1Start) return SlotState::Slot1;
    return SlotState::SwitchMtoO;
}

Band Scheduler::band_at(int phase_ms) {
    const SlotState s = state_at(phase_ms);
    return (s == SlotState::Slot0 || s == SlotState::Hop || s == SlotState::Slot1) ? Band::M
                                                                                   : Band::O;
}

uint32_t Scheduler::freq_at(int phase_ms) {
    switch (state_at(phase_ms)) {
        case SlotState::Slot0:
        case SlotState::Hop: return kMband0Hz;
        case SlotState::Slot1: return kMband1Hz;
        default: return kObandHz;
    }
}

int Scheduler::slot_of(int phase_ms) {
    if (phase_ms < kSlot1Wrap) return 1;
    if (phase_ms >= kSlot0Start && phase_ms < kSlot0End) return 0;
    if (phase_ms >= kSlot1Start) return 1;
    return -1;
}

SlotPlan Scheduler::plan(int phase_ms, const ClockState& clock) const {
    if (phase_ms < 0) phase_ms = 0;
    if (phase_ms >= 1000) phase_ms %= 1000;

    SlotPlan p{};
    p.state = state_at(phase_ms);
    p.band = band_at(phase_ms);
    p.freq_hz = freq_at(phase_ms);
    p.own_tx_slot = in_direct_slot(phase_ms);

    switch (p.state) {
        case SlotState::Slot0:
        case SlotState::Hop:
            p.start_ms = kSlot0Start;
            p.end_ms = kSlot0End - kHopGuardMs;
            break;
        case SlotState::Slot1:
            p.start_ms = kSlot1Start;
            p.end_ms = kSlot1End;
            break;
        default:
            // One O-band dwell, framed 5 ms either side of the window our own
            // ground station transmits in.
            p.start_ms = kUplinkRxStart;
            p.end_ms = kUplinkRxEnd;
            break;
    }

    const bool anchored = clock.utc_valid && clock.pps_locked;
    const bool within_holdover =
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

}  // namespace skyblip::timing
