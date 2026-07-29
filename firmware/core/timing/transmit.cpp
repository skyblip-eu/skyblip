#include "core/timing/transmit.h"

namespace skyblip::timing {

namespace {
uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
}  // namespace

// The M-band dwells are wider than the direct slot at both ends: they open at
// 400 ms to catch traffic that transmits from 405, and the second one runs to
// 1200 ms to hear traffic still using its own slot there. Our own burst lives
// inside the direct slot, 450..1000 (§C.5), and completes inside both: the slot,
// because the standard says so, and the dwell, because hopping channels mid-burst
// would truncate it on air.
int Transmitter::instant_in(int slot, uint32_t utc) const {
    const int opens = Scheduler::slot_start(slot);
    const int first = opens > kDirectStart ? opens : kDirectStart;
    const int slot_end = Scheduler::slot_end(slot);
    const int closes = slot_end < kDirectEnd ? slot_end : kDirectEnd;
    const int last = closes - kCompletionSlackMs - static_cast<int>(kAirTimeMs);
    if (last <= first) return first;
    // Inclusive of last: a burst starting there still ends inside the slack.
    return first +
           static_cast<int>(mix(addr_ ^ mix(utc)) % static_cast<uint32_t>(last - first + 1));
}

Transmitter::Attempt Transmitter::attempt(const SlotPlan& plan, uint32_t utc, uint32_t now_ms,
                                          bool airborne, uint32_t fix_age_ms) const {
    Attempt a{};
    if (!plan.tx_allowed) return a;
    if (fix_age_ms > kFixAgeMaxMs) return a;
    if (now_ms < quiet_until_ms_) return a;
    if (ever_sent_ && airborne && utc == last_sent_utc_) return a;
    if (ever_sent_ && !airborne && now_ms - last_sent_ms_ < kGroundPeriodMs) return a;

    const int slot = next_slot();
    if (Scheduler::slot_of(plan.start_ms) != slot) return a;

    a.go = true;
    a.at_ms = instant_in(slot, utc);
    a.freq_hz = Scheduler::slot_freq(slot);
    a.force = first_attempt_ms_ != 0 && now_ms - first_attempt_ms_ >= kForceAfterMs;
    return a;
}

void Transmitter::sent(uint32_t utc, uint32_t now_ms, bool forced) {
    sent_++;
    ever_sent_ = true;
    last_sent_utc_ = utc;
    last_sent_ms_ = now_ms;
    first_attempt_ms_ = 0;
    quiet_until_ms_ = forced ? now_ms + kQuietAfterForcedMs : 0;
}

void Transmitter::busy(uint32_t now_ms) {
    busy_++;
    if (first_attempt_ms_ == 0) first_attempt_ms_ = now_ms;
}

}  // namespace skyblip::timing
