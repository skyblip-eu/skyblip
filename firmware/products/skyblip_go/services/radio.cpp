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
    take_carrier_samples();
    collect_outcome(now_ms);
    // The threshold the next dwell will carry, on the bus for the companion
    // link: EN 300 220-2 V3.3.1 §4.6.2.3 evidence, published after the outcome
    // that moved the retry rather than before it.
    context_.state.carrier_sense_dbm = lbt_threshold_dbm();

    const hal::RfMode want = mode_for(plan);
    const bool same_dwell = want == armed_ && plan.freq_hz == armed_freq_;
    if (!same_dwell || transmit_due(plan, now_ms)) arm_dwell(plan, now_ms);
    publish_dwell(now_ms);
}

// The one place the phase this service arms against leaves it. core/timing's
// durable-write policy needs the radio's own view of the second, not a second
// copy of the slot arithmetic, and it needs to know a burst is in flight - which
// only the arming code and the outcome collector between them can say.
void RadioService::publish_dwell(uint32_t now_ms) {
    timing::DwellPhase& dwell = context_.state.dwell;
    dwell.at_ms = now_ms;
    dwell.phase_ms = phase_ms(now_ms);
    dwell.armed = armed_ != hal::RfMode::Idle;
    dwell.burst_armed = tx_armed_;
}

// From the latched edge, at the instant it is asked for. Deriving it from a
// phase the board sampled at the top of the pass costs however long the pass
// takes to reach this service, which is the whole jitter guard on a bad pass.
int RadioService::phase_ms(uint32_t now_ms) const {
    const timing::ClockState& clock = context_.state.clock;
    const uint64_t now_us = context_.roles.clock.micros();
    if (clock.pps_locked && now_us >= clock.pps_edge_us)
        return static_cast<int>((now_us - clock.pps_edge_us) / 1000 % 1000);
    return static_cast<int>(now_ms % 1000);
}

// The executor assesses, the policy averages what the assessments report. One
// assessment per pass is all the executor can have made between two of them at
// a 15 ms minimum backoff.
void RadioService::take_carrier_samples() {
    const hal::RfCarrier carrier = context_.roles.rf.carrier();
    if (carrier.samples == seen_carrier_samples_) return;
    seen_carrier_samples_ = carrier.samples;
    noise_.sample(carrier.dbm);
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

// Two clocks meet here. The TimeStamp field counts quarter seconds from the top
// of the UTC second the slot belongs to, which the latched PPS edge anchors;
// the position has to be carried over the interval since the fix, which is a
// monotonic one. The dwell is armed before the slot opens, so the second figure
// is the staleness already accrued plus the whole wait still to come.
protocol::BurstInstant RadioService::burst_instant(const timing::Transmitter::Attempt& a,
                                                   uint64_t tx_at_us, uint32_t now_ms) const {
    const uint32_t tx_ms = static_cast<uint32_t>(tx_at_us / 1000);
    protocol::BurstInstant at{};
    at.utc = slot_utc(now_ms);
    at.into_utc_ms = a.at_ms;
    at.since_fix_ms = static_cast<int32_t>(tx_ms - context_.state.own.fix_ms);
    return at;
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
// ground station's uplink and nothing else. Both halves of a dwell are set
// here: what to listen for, and the modulation to listen with - §C.4 runs at
// twice §C.2's chip rate through a Gaussian filter and a wider receiver, so a
// dwell that only moved the synthesiser was tuned to the O band and deaf on it.
void RadioService::listen_for(timing::Band band, hal::RfPlan& plan) {
    if (band == timing::Band::O) {
        plan.sync = protocol::kUplinkSync;
        plan.sync_bits = protocol::kUplinkSyncBits;
        plan.rx_len = protocol::kUplinkFrameBytes;
        plan.bitrate = protocol::kUplinkChipRateBps;
        plan.fdev_hz = protocol::kUplinkDeviationHz;
        plan.bandwidth_hz = protocol::kUplinkChannelBandwidthHz;
        plan.gaussian_bt_e2 = protocol::kUplinkGaussianBtE2;
        return;
    }
    plan.sync = protocol::kSharedSync;
    plan.sync_bits = protocol::kSharedSyncBits;
    plan.rx_len = protocol::kRxChipBytes;
    plan.bitrate = protocol::kMbandChipRateBps;
    plan.fdev_hz = protocol::kMbandDeviationHz;
    plan.bandwidth_hz = protocol::kMbandChannelBandwidthHz;
    plan.gaussian_bt_e2 = protocol::kMbandGaussianBtE2;
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
    // F5: a cold receiver's first solutions walk, and the flight state derived
    // from them decides our transmit rate. Nothing goes on air until own-ship
    // says the solution behind it has settled.
    if (!own.fix_valid || !own.utc_valid || !own.tx_settled) return timing::Transmitter::Attempt{};
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
    over_budget_ = a.over_budget;
    // The one place this policy's own refusal is decided: a plan the hour's
    // air-time budget already refused to arm, counted apart from a dwell that
    // was armed and then missed its outcome.
    if (a.over_budget) context_.state.timing_stats.record_refused();
    const uint64_t tx_at_us = epoch_us + static_cast<uint64_t>(a.at_ms) * 1000;
    const bool carries_tx = a.go && tx_at_us >= plan.start_us && tx_at_us < plan.end_us;
    if (carries_tx) {
        protocol::from_own(outgoing_, context_.state.own, context_.roles.device_addr,
                           context_.state.settings.addr_table, context_.state.own.aircraft_cat,
                           context_.state.settings.stealth, burst_instant(a, tx_at_us, now_ms));
        outgoing_.scramble();
        outgoing_.set_crc();
        plan.tx = outgoing_chips_;
        plan.tx_len = static_cast<uint8_t>(protocol::encode_mband(
            protocol::kAdslSyncWord, reinterpret_cast<const uint8_t*>(&outgoing_.Version),
            protocol::kAdslFrameBytes, outgoing_chips_));
        plan.tx_at_us = tx_at_us;
        plan.lbt = !a.force;
        plan.lbt_threshold_dbm = noise_.threshold_dbm(lbt_retry_);
        plan.backoff_min_ms = timing::Transmitter::kBackoffMinMs;
        plan.backoff_max_ms = timing::Transmitter::kBackoffMaxMs;
    }

    if (context_.roles.rf.arm(plan) != Status::Ok) {
        // A dwell refused before it could even start: hal::Rf's own "a plan
        // that cannot complete before its end is refused here rather than
        // truncated on air", read out on the bench.
        context_.state.timing_stats.record_missed();
        return;
    }
    armed_ = plan.mode;
    armed_freq_ = plan.freq_hz;
    arm_count_++;
    tx_armed_ = carries_tx;
    if (carries_tx) {
        tx_utc_ = slot_utc(now_ms);
        tx_forced_ = a.force;
        tx_end_us_ = plan.end_us;
        tx_deadline_us_ = tx_at_us;
    }
}

// The executor reports on the bus, which the traffic service drains into the
// counters: a transmission that made it, and one the carrier never cleared for.
void RadioService::collect_outcome(uint32_t now_ms) {
    if (context_.state.tx_ok != seen_tx_ok_) {
        seen_tx_ok_ = context_.state.tx_ok;
        transmitter_.sent(tx_utc_, now_ms, tx_forced_);
        // The executor's own report against the deadline this dwell was armed
        // for: both absolute instants on the same clock, so slot 1's wrap
        // costs this nothing.
        context_.state.timing_stats.record_dwell_phase(
            static_cast<int64_t>(context_.state.last_tx_done_at_us) -
            static_cast<int64_t>(tx_deadline_us_));
        lbt_retry_ = 0;
        tx_armed_ = false;
    }
    if (context_.state.tx_busy != seen_tx_busy_) {
        seen_tx_busy_ = context_.state.tx_busy;
        transmitter_.busy(now_ms);
        // A dwell that never got a word in buys the next one 3 dB of tolerance
        // (oss/nrf52-ogn-tracker src/ogn-radio.cpp:851, which escalates inside
        // one slot; our dwell is one carrier sample per backoff interval, so the
        // escalation is per dwell and §D.3's forced transmission is what ends it).
        if (lbt_retry_ < 0xFF) lbt_retry_++;
        tx_armed_ = false;
    }
    // A dwell that ended without either report above took the radio with it:
    // the policy must not stay armed on a burst that will never be reported,
    // and on the bench this is the one outcome no other counter watches for.
    // Checked last, so a report that arrived this same pass is not also
    // counted as missed.
    if (tx_armed_ && context_.roles.clock.micros() >= tx_end_us_) {
        context_.state.timing_stats.record_missed();
        tx_armed_ = false;
    }
}

}  // namespace skyblip::go
