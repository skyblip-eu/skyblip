// The single-radio dwell map of ADS-L 4 SRD-860 issue 2 §C.5 (time
// multiplexing), §C.2 (M-band channels) and §C.4 (O-band HDR).
#ifndef SKYBLIP_CORE_TIMING_SLOT_H
#define SKYBLIP_CORE_TIMING_SLOT_H

#include <cstdint>

namespace skyblip::timing {

// §C.2: two 200 kHz M-band channels. §C.4: one O-band HDR channel.
constexpr uint32_t kMband0Hz = 868200000;
constexpr uint32_t kMband1Hz = 868400000;
constexpr uint32_t kObandHz = 869525000;

// §C.5 gives the second to the bands: 200..450 ms is the uplink slot the ground
// infrastructure transmits in, 450..1000 the direct slot. One radio cannot hold
// two bands at once, so the second is cut where our own ground station stops
// talking rather than where the slot formally ends: a skyPost uplink burst
// completes by 390 ms.
//
// The two M-band dwells are 400 ms and 400 ms, one channel each (§C.2.5 makes
// traffic alternate channels, so a receiver that never visits 868.4 MHz hears
// half of its neighbours). The second dwell therefore spans the UTC second: it
// opens at 800 ms and closes at 1200, which is 200 ms into the next second. That
// tail belongs to the dwell, not to the new second, and it is RECEIVE ONLY:
// FLARM-generation traffic is still transmitting there, while §C.5 ends the
// direct slot at 1000 ms and we stop with it.
constexpr int kGroundEmitStart = 210;
constexpr int kGroundEmitEnd = 390;
constexpr int kUplinkRxStart = 205;
constexpr int kUplinkRxEnd = 395;
constexpr int kSlot0Start = 400;
constexpr int kSlot0End = 800;
constexpr int kSlot1Start = 800;
constexpr int kSlot1End = 1200;
// How far slot 1 reaches past the second it started in.
constexpr int kSlot1Wrap = kSlot1End - 1000;
// The ADS-L direct slot, which our own bursts respect on top of the dwell map:
// we listen on M-band from 400 ms, we start transmitting at 450.
constexpr int kDirectStart = 450;
constexpr int kDirectEnd = 1000;
// Both band edges get the same guard: slot 1 ends at 200, uplink RX starts at
// 205. The uplink dwell ends at 395, M-band opens at 400. The O->M edge is the
// safety-critical one - traffic is on air there and the radio has to be tuned,
// not tuning.
constexpr int kJitterGuardMs = 5;
constexpr int kHopGuardMs = 1;

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
    // The latched edge itself, on the same clock hal::Rf deadlines are armed
    // against. A phase sampled once per service pass is stale by however long
    // that pass takes; an absolute instant is not, so the phase is derived from
    // this at the point of use and the slot map keeps its 5 ms guard.
    uint64_t pps_edge_us{0};
};

struct SlotPlan {
    SlotState state{SlotState::UplinkRxO};
    Band band{Band::O};
    uint32_t freq_hz{kObandHz};
    // The dwell this state belongs to, as a phase in the second. Guards are
    // already subtracted: a dwell that runs to its end_ms leaves the radio time
    // to retune before the next one starts.
    int start_ms{0};
    int end_ms{0};
    bool tx_allowed{false};
    bool own_tx_slot{false};
    bool listen_only{true};
};

class Scheduler {
   public:
    SlotPlan plan(int phase_ms, const ClockState& clock) const;

    static SlotState state_at(int phase_ms);
    static Band band_at(int phase_ms);
    static uint32_t freq_at(int phase_ms);
    static bool in_uplink_rx(int phase_ms) {
        return phase_ms >= kUplinkRxStart && phase_ms < kUplinkRxEnd;
    }
    // Where own-ship may transmit, which is narrower at both ends than what the
    // M-band dwells listen to: they open 50 ms before the direct slot does and
    // close 200 ms after it.
    static bool in_direct_slot(int phase_ms) {
        return phase_ms >= kDirectStart && phase_ms < kDirectEnd;
    }
    // Which M-band dwell, and so which channel, a phase belongs to. -1 outside
    // the M-band half of the second.
    static int slot_of(int phase_ms);
    static uint32_t slot_freq(int slot) { return slot == 1 ? kMband1Hz : kMband0Hz; }
    static int slot_start(int slot) { return slot == 1 ? kSlot1Start : kSlot0Start; }
    static int slot_end(int slot) { return slot == 1 ? kSlot1End : kSlot0End; }
};

}  // namespace skyblip::timing

#endif
