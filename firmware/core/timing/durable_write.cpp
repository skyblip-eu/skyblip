#include "core/timing/durable_write.h"

namespace skyblip::timing {

namespace {
constexpr int kSecondMs = 1000;
}  // namespace

// Four refusals, in the order they cost. The first is the loudest: an armed dwell
// hands the radio a plan, and the CPU's remaining debt to it is (a) the instant it
// must be re-armed, (b) the instant own-ship keys the PA inside it, and (c) the
// PPS edge the next second's deadlines are all measured from. A stall that lands
// on the third is the worst of the three, because it does not delay one dwell -
// it moves the anchor under every dwell that follows.
bool DurableWriteWindow::free_at(const SlotPlan& plan, int phase_ms, uint32_t cost_ms) {
    if (plan.tx_allowed) return false;
    if (phase_ms < 0) return false;
    if (phase_ms >= kSecondMs) phase_ms -= kSecondMs;

    // Slot 1 opened 800 ms into the previous second and closes 200 ms into this
    // one, so inside its tail the phase, the dwell's end and the next PPS edge
    // are all counted in the frame the dwell started in.
    int at_ms = phase_ms;
    if (plan.end_ms > kSecondMs && at_ms < plan.end_ms - kSecondMs) at_ms += kSecondMs;
    const int done_ms = at_ms + static_cast<int>(cost_ms);

    // A dwell that has not opened yet is a dwell the core still has to arm, and
    // the two 5 ms retune guards the map leaves between dwells are exactly where
    // that arming happens. So a write goes inside a dwell that is already running,
    // never in the gap before one.
    if (at_ms < plan.start_ms) return false;
    if (done_ms + kJitterGuardMs > plan.end_ms) return false;
    // §C.5's direct slot is narrower than the M-band dwells at both ends, so a
    // write placed in slot 0's head would still be holding the core at 450 ms,
    // which is an instant core/timing::Transmitter is allowed to have chosen.
    if (at_ms < kDirectStart && done_ms + kJitterGuardMs > kDirectStart) return false;
    const int next_edge_ms = at_ms < kSecondMs ? kSecondMs : 2 * kSecondMs;
    if (done_ms + kJitterGuardMs > next_edge_ms) return false;
    return true;
}

void DurableWriteWindow::request(uint32_t now_ms) {
    requests_++;
    if (!pending_) {
        pending_ = true;
        first_request_ms_ = now_ms;
    }
    last_request_ms_ = now_ms;
}

bool DurableWriteWindow::placeable(const SlotPlan& plan, const DwellPhase& dwell,
                                   uint32_t now_ms) const {
    // Nothing is armed: a product with no radio fitted, or one before the first
    // dwell. There is no second to respect.
    if (!dwell.armed) return true;
    // A burst is on the plan and its outcome has not been reported. That dwell is
    // the one the whole guard exists for, whatever phase it is at.
    if (dwell.burst_armed) return false;
    const uint32_t stale_ms = now_ms - dwell.at_ms;
    if (stale_ms > kViewStaleMs) return false;
    return free_at(plan, dwell.phase_ms + static_cast<int>(stale_ms), kWorstWriteMs);
}

DurableWriteVerdict DurableWriteWindow::decide(const SlotPlan& plan, const DwellPhase& dwell,
                                               uint32_t now_ms) const {
    if (!pending_) return DurableWriteVerdict::Idle;
    // The bound outranks the coalescing settle, and then the window: a thumb that
    // keeps stepping a value for longer than the bound buys a second write, which
    // is the trade this way round on purpose.
    const bool bound_spent = now_ms - first_request_ms_ >= kMaxDeferMs;
    if (!bound_spent && now_ms - last_request_ms_ < kSettleMs) return DurableWriteVerdict::Hold;
    if (placeable(plan, dwell, now_ms)) return DurableWriteVerdict::Place;
    return bound_spent ? DurableWriteVerdict::Forced : DurableWriteVerdict::Hold;
}

void DurableWriteWindow::placed(uint32_t now_ms, bool forced) {
    const uint32_t waited_ms = now_ms - first_request_ms_;
    if (waited_ms > worst_wait_ms_) worst_wait_ms_ = waited_ms;
    writes_++;
    if (forced) forced_++;
    pending_ = false;
}

}  // namespace skyblip::timing
