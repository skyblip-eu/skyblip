#include "products/skyblip_go/services/radio.h"

namespace skyblip::go {

Status RadioService::setup() {
    if (!hal::has(context_.roles.capabilities, hal::Capability::Rf)) return Status::Down;
    transmitter_.configure(context_.roles.device_addr);
    arm_dwell(scheduler_.plan(0, context_.state.clock), 0);
    return Status::Ok;
}

void RadioService::tick(uint32_t now_ms) {
    const timing::SlotPlan plan = scheduler_.plan(phase_ms(now_ms), context_.state.clock);
    context_.state.plan = plan;
    collect_outcome(now_ms);

    const hal::RfMode want = mode_for(plan);
    const bool same_dwell = want == armed_ && plan.freq_hz == armed_freq_;
    if (same_dwell && !transmit_due(plan, now_ms)) return;
    arm_dwell(plan, now_ms);
}

int RadioService::phase_ms(uint32_t now_ms) const {
    if (context_.state.clock.pps_locked)
        return static_cast<int>(context_.state.clock.ms_since_pps % 1000);
    return static_cast<int>(now_ms % 1000);
}

uint64_t RadioService::pps_epoch_us(uint32_t now_ms) const {
    const uint64_t now_us = context_.roles.clock.micros();
    const uint64_t phase_us = static_cast<uint64_t>(phase_ms(now_ms)) * 1000;
    return now_us > phase_us ? now_us - phase_us : 0;
}

uint64_t RadioService::dwell_epoch_us(uint32_t now_ms) const {
    const uint64_t epoch_us = pps_epoch_us(now_ms);
    const bool in_slot1_tail = phase_ms(now_ms) < timing::kSlot1Wrap;
    return (in_slot1_tail && epoch_us >= 1000000) ? epoch_us - 1000000 : epoch_us;
}

uint32_t RadioService::slot_utc(uint32_t now_ms) const {
    const uint32_t utc = context_.state.own.utc;
    const bool in_slot1_tail = phase_ms(now_ms) < timing::kSlot1Wrap;
    return (in_slot1_tail && utc > 0) ? utc - 1 : utc;
}

hal::RfMode RadioService::mode_for(const timing::SlotPlan& plan) {
    return plan.band == timing::Band::O ? hal::RfMode::RxOband : hal::RfMode::RxMband;
}

// The M band carries two systems past one sync window. The O band carries the
// ground station's uplink and nothing else.
void RadioService::listen_for(timing::Band band, hal::RfPlan& plan) {
    if (band == timing::Band::O) {
        plan.sync = protocol::kUplinkSync;
        plan.sync_bits = protocol::kUplinkSyncBits;
        plan.rx_len = static_cast<uint8_t>(protocol::AdslUplink::kFrameBytes);
        return;
    }
    plan.sync = protocol::kSharedSync;
    plan.sync_bits = protocol::kSharedSyncBits;
    plan.rx_len = protocol::kRxChipBytes;
}

// The dwell already armed carries this second's burst, or there is none to
// carry: re-arming would only restart the receiver mid-slot.
bool RadioService::transmit_due(const timing::SlotPlan& plan, uint32_t now_ms) const {
    if (tx_armed_) return false;
    return attempt(plan, now_ms).go;
}

timing::Transmitter::Attempt RadioService::attempt(const timing::SlotPlan& plan,
                                                   uint32_t now_ms) const {
    const messages::OwnState& own = context_.state.own;
    if (!own.fix_valid || !own.utc_valid) return timing::Transmitter::Attempt{};
    const uint32_t fix_age_ms = now_ms - own.fix_ms;
    const bool airborne = own.flight_state == kFlightStateAirborne;
    return transmitter_.attempt(plan, slot_utc(now_ms), now_ms, airborne, fix_age_ms);
}

void RadioService::arm_dwell(const timing::SlotPlan& slot, uint32_t now_ms) {
    const uint64_t epoch_us = dwell_epoch_us(now_ms);
    const uint64_t now_us = context_.roles.clock.micros();

    hal::RfPlan plan{};
    plan.mode = mode_for(slot);
    plan.freq_hz = slot.freq_hz;
    listen_for(slot.band, plan);
    plan.start_us = epoch_us + static_cast<uint64_t>(slot.start_ms) * 1000;
    plan.end_us = epoch_us + static_cast<uint64_t>(slot.end_ms) * 1000;
    // Arming inside the window means the dwell has already started: begin now and
    // keep the same hard stop, rather than waiting a whole second for the next one.
    if (plan.start_us < now_us) plan.start_us = now_us;
    if (plan.end_us <= plan.start_us) plan.end_us = plan.start_us + 1000;

    const timing::Transmitter::Attempt a = attempt(slot, now_ms);
    const uint64_t tx_at_us = epoch_us + static_cast<uint64_t>(a.at_ms) * 1000;
    const bool carries_tx = a.go && tx_at_us >= plan.start_us && tx_at_us < plan.end_us;
    if (carries_tx) {
        protocol::from_own(outgoing_, context_.state.own, context_.roles.device_addr,
                           context_.state.settings.addr_table, context_.state.own.aircraft_cat,
                           context_.state.settings.stealth);
        outgoing_.scramble();
        outgoing_.set_crc();
        plan.tx = outgoing_chips_;
        plan.tx_len = static_cast<uint8_t>(protocol::encode_mband(
            protocol::kAdslSyncWord, reinterpret_cast<const uint8_t*>(&outgoing_.Version),
            protocol::kAdslFrameBytes, outgoing_chips_));
        plan.tx_at_us = tx_at_us;
        plan.lbt = !a.force;
        plan.lbt_threshold_dbm = timing::Transmitter::kBusyThresholdDbm;
        plan.backoff_min_ms = timing::Transmitter::kBackoffMinMs;
        plan.backoff_max_ms = timing::Transmitter::kBackoffMaxMs;
    }

    if (context_.roles.rf.arm(plan) != Status::Ok) return;
    armed_ = plan.mode;
    armed_freq_ = plan.freq_hz;
    arm_count_++;
    tx_armed_ = carries_tx;
    if (carries_tx) {
        tx_utc_ = slot_utc(now_ms);
        tx_forced_ = a.force;
        tx_end_us_ = plan.end_us;
    }
}

// The executor reports on the bus, which the traffic service drains into the
// counters: a transmission that made it, and one the carrier never cleared for.
void RadioService::collect_outcome(uint32_t now_ms) {
    // A dwell that ended without either report took the radio with it: the
    // policy must not stay armed on a burst that will never be reported.
    if (tx_armed_ && context_.roles.clock.micros() >= tx_end_us_) tx_armed_ = false;
    if (context_.state.tx_ok != seen_tx_ok_) {
        seen_tx_ok_ = context_.state.tx_ok;
        transmitter_.sent(tx_utc_, now_ms, tx_forced_);
        tx_armed_ = false;
    }
    if (context_.state.tx_busy != seen_tx_busy_) {
        seen_tx_busy_ = context_.state.tx_busy;
        transmitter_.busy(now_ms);
        tx_armed_ = false;
    }
}

}  // namespace skyblip::go
