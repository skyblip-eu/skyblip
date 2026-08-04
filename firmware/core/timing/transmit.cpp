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
    if (quiet_ && now_ms - quiet_since_ms_ < kQuietAfterForcedMs) return a;
    if (ever_sent_ && airborne && utc == last_sent_utc_) return a;
    if (ever_sent_ && !airborne && now_ms - last_sent_ms_ < kGroundPeriodMs) return a;

    const int slot = next_slot();
    if (Scheduler::slot_of(plan.start_ms) != slot) return a;

    // EN 300 220-2 V3.3.1 Table 4 band M is 1% of any hour, and that limit is
    // the channel-access route this product declares: the budget is not a
    // fallback behind listen-before-talk, it is the mechanism. At the design
    // rate of one 5 ms burst per second we sit at half the allowance, so an
    // empty budget is a fault, and a faulted transmitter that will not stop is
    // worse for everyone on the band than a quiet one. It blocks, and the forced
    // transmission of §D.3 does not get an exemption from it.
    if (!air_.may_spend(now_ms, kAirTimeMs)) {
        a.over_budget = true;
        return a;
    }

    a.go = true;
    a.at_ms = instant_in(slot, utc);
    a.freq_hz = Scheduler::slot_freq(slot);
    a.force = attempting_ && now_ms - attempt_since_ms_ >= kForceAfterMs;
    return a;
}

void Transmitter::sent(uint32_t utc, uint32_t now_ms, bool forced) {
    sent_++;
    air_.spend(now_ms, kAirTimeMs);
    ever_sent_ = true;
    last_sent_utc_ = utc;
    last_sent_ms_ = now_ms;
    attempting_ = false;
    quiet_ = forced;
    quiet_since_ms_ = now_ms;
}

void Transmitter::busy(uint32_t now_ms) {
    busy_++;
    if (attempting_) return;
    attempting_ = true;
    attempt_since_ms_ = now_ms;
}

}  // namespace skyblip::timing
