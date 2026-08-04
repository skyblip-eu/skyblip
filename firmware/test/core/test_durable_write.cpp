// core/timing's durable-write policy, alone: a slot plan, a phase, a clock the
// case advances, and no flash anywhere near it.
//
// The thing being pinned down is that 1-ARCHITECTURE.md §5.1's "no flash work
// inside a dwell" has to be read as a statement about deadlines, because the
// dwell map leaves no unarmed phase to write in: 0..200 is slot 1's tail, 205..395
// the uplink dwell, 400..1200 the two M-band dwells, and the only two gaps are the
// 5 ms retune guards, which are the worst place in the second for an 85 ms stall.
// So the cases below say where the stall IS allowed, and prove that the set is the
// two stretches the map's own numbers leave.
#include "core/timing/durable_write.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::timing;

namespace {

// The plan the radio would have armed at this phase, with the clock anchored -
// which is the only state in which own-ship may key the PA at all.
SlotPlan anchored_plan(int phase_ms) {
    ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    Scheduler scheduler{};
    return scheduler.plan(phase_ms, clock);
}

// The same second with no PPS lock but a believed UTC: the map still runs, the
// dwells are still armed, and nothing goes on air.
SlotPlan holdover_plan(int phase_ms) {
    ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = false;
    clock.ms_since_pps = 10000;
    Scheduler scheduler{};
    return scheduler.plan(phase_ms, clock);
}

// One settings write, at this phase, against the plan the radio would have armed.
bool free_at(int phase_ms) {
    return DurableWriteWindow::free_at(anchored_plan(phase_ms), phase_ms,
                                       DurableWriteWindow::kWorstWriteMs);
}

bool free_in_holdover_at(int phase_ms) {
    return DurableWriteWindow::free_at(holdover_plan(phase_ms), phase_ms,
                                       DurableWriteWindow::kWorstWriteMs);
}

DwellPhase view_at(int phase_ms, uint32_t at_ms) {
    DwellPhase dwell{};
    dwell.armed = true;
    dwell.phase_ms = phase_ms;
    dwell.at_ms = at_ms;
    return dwell;
}

}  // namespace

// The datasheet, in the code. If either of these two figures ever moves, every
// window arithmetic below moves with it and this is the case that says so.
TEST_CASE("durable write: the cost is the nRF52840's own page erase plus the copy") {
    CHECK(DurableWriteWindow::kPageEraseMs == 85);
    CHECK(DurableWriteWindow::kWordWriteUs == 41);
    // 85 ms of erase, plus 16 words of settings blob copied forward at 41 us each,
    // rounded up to the millisecond.
    CHECK(DurableWriteWindow::kWorstWriteMs == 86);
    // The whole worst case fits inside a free stretch, which is what makes
    // deferring a sufficient answer rather than only half of one. The other half is
    // the partial-erase slice, which bounds the longest SINGLE stall to less than
    // the guard the slot map already absorbs.
    CHECK(DurableWriteWindow::kPartialEraseMs < static_cast<uint32_t>(kJitterGuardMs));
}

// The whole second, phase by phase. Two stretches and no others:
//   0..109   slot 1's tail, which closes at 200 (kSlot1Wrap) - the write finishes
//            by 195 and the 5 ms guard before the uplink dwell is still whole.
//   205..304 the uplink dwell, which closes at 395 - the write finishes by 390 and
//            the guard in front of the safety-critical O->M edge is still whole.
TEST_CASE("durable write: the free phases are the two the dwell map leaves") {
    int first_free = -1;
    int last_free = -1;
    int stretches = 0;
    bool was_free = false;
    for (int phase = 0; phase < 1000; phase++) {
        const bool free = free_at(phase);
        if (free && !was_free) {
            stretches++;
            if (first_free < 0) first_free = phase;
        }
        if (free) last_free = phase;
        was_free = free;
    }
    CHECK(stretches == 2);
    CHECK(first_free == 0);
    CHECK(last_free == 304);

    // Named, so a moved constant fails here and not in a product case.
    CHECK(free_at(109));
    CHECK_FALSE(free_at(110));
    CHECK(free_at(205));
    CHECK(free_at(304));
    CHECK_FALSE(free_at(305));
}

// The two retune guards, 200..204 and 395..399, are the gaps between dwells. They
// are also where the next dwell gets armed, so they are refused rather than taken
// for free time: a write there delays the opening of the dwell that follows.
TEST_CASE("durable write: the retune guards between dwells are not free time") {
    for (int phase = kSlot1Wrap; phase < kUplinkRxStart; phase++) CHECK_FALSE(free_at(phase));
    for (int phase = kUplinkRxEnd; phase < kSlot0Start; phase++) CHECK_FALSE(free_at(phase));
}

// Slot 0 opens at 400 and the direct slot at 450: a write placed in slot 0's head
// would still be holding the core at 450, which is an instant core/timing's
// Transmitter is allowed to have chosen to key the PA at.
TEST_CASE("durable write: slot 0's head is refused because the direct slot follows it") {
    for (int phase = kSlot0Start; phase < kDirectStart; phase++) CHECK_FALSE(free_at(phase));
    for (int phase = kDirectStart; phase < 1000; phase++) CHECK_FALSE(free_at(phase));
}

// With no lock there is no transmission, so slot 1's body is genuinely free -
// except for the phases where the write would still be running when the top of the
// second arrives. That edge is timestamped inside the GPIO callback
// (hardware/platform/zephyr/pps.h), so a stall that spans it does not delay one
// dwell: it moves the anchor under every dwell that follows.
TEST_CASE("durable write: a write is never allowed to span the top of the second") {
    CHECK(free_in_holdover_at(800));
    // 909 is the last phase whose write still finishes a whole guard before the
    // edge: 909 + 86 = 995, and the guard runs to 1000 exactly.
    CHECK(free_in_holdover_at(909));
    for (int phase = 910; phase < 1000; phase++) CHECK_FALSE(free_in_holdover_at(phase));
}

TEST_CASE("durable write: nothing pending is not a decision to make") {
    DurableWriteWindow window;
    CHECK(window.decide(anchored_plan(0), view_at(0, 0), 0) == DurableWriteVerdict::Idle);
    CHECK(window.writes() == 0);
}

// Six taps stepping a value: one blob, one write. The settle is what does it, and
// the blob is only read at the instant it is written, so what lands is the last
// value and not six of them.
TEST_CASE("durable write: rapid changes coalesce into one placement") {
    DurableWriteWindow window;
    uint32_t t = 10000;
    for (int i = 0; i < 6; i++) {
        window.request(t);
        t += 150;
        CHECK(window.decide(anchored_plan(0), view_at(0, t), t) == DurableWriteVerdict::Hold);
    }
    CHECK(window.requests() == 6);

    t += DurableWriteWindow::kSettleMs;
    REQUIRE(window.decide(anchored_plan(50), view_at(50, t), t) == DurableWriteVerdict::Place);
    window.placed(t, false);
    CHECK(window.writes() == 1);
    CHECK(window.forced() == 0);
    CHECK_FALSE(window.pending());
}

// A dwell carrying a burst whose outcome has not come back is refused at any
// phase: that is the one dwell the whole guard exists for.
TEST_CASE("durable write: a burst in flight refuses the write whatever the phase") {
    DurableWriteWindow window;
    window.request(0);
    const uint32_t t = DurableWriteWindow::kSettleMs;
    DwellPhase dwell = view_at(0, t);
    dwell.burst_armed = true;
    CHECK(window.decide(anchored_plan(0), dwell, t) == DurableWriteVerdict::Hold);
    dwell.burst_armed = false;
    CHECK(window.decide(anchored_plan(0), dwell, t) == DurableWriteVerdict::Place);
}

// A view nobody refreshed is not evidence about where the second is.
TEST_CASE("durable write: a stale view of the second refuses rather than guesses") {
    DurableWriteWindow window;
    window.request(0);
    const uint32_t t = DurableWriteWindow::kSettleMs;
    const DwellPhase stale = view_at(0, t - DurableWriteWindow::kViewStaleMs - 1);
    CHECK(window.decide(anchored_plan(0), stale, t) == DurableWriteVerdict::Hold);
    CHECK(window.decide(anchored_plan(0), view_at(0, t), t) == DurableWriteVerdict::Place);
}

// A product with no radio fitted has no second to respect.
TEST_CASE("durable write: with no dwell armed there is nothing to wait for") {
    DurableWriteWindow window;
    window.request(0);
    DwellPhase dwell{};
    dwell.armed = false;
    const uint32_t t = DurableWriteWindow::kSettleMs;
    CHECK(window.decide(anchored_plan(500), dwell, t) == DurableWriteVerdict::Place);
}

// The bound outranks the settle. A thumb that keeps stepping a value for longer
// than kMaxDeferMs buys a second write rather than an unbounded wait, which is the
// trade this way round on purpose: a setting still not on flash when the cell dies
// is a setting the pilot will believe they changed.
TEST_CASE("durable write: the bound outranks the coalescing settle") {
    DurableWriteWindow window;
    uint32_t t = 0;
    window.request(t);
    while (t < DurableWriteWindow::kMaxDeferMs) {
        t += 100;
        window.request(t);
        if (t < DurableWriteWindow::kMaxDeferMs)
            CHECK(window.decide(anchored_plan(500), view_at(500, t), t) ==
                  DurableWriteVerdict::Hold);
    }
    // Past the bound, at a phase that will never be free: it goes anyway, and it
    // is counted as the fault it is.
    REQUIRE(window.decide(anchored_plan(500), view_at(500, t), t) == DurableWriteVerdict::Forced);
    window.placed(t, true);
    CHECK(window.writes() == 1);
    CHECK(window.forced() == 1);
    CHECK(window.worst_wait_ms() >= DurableWriteWindow::kMaxDeferMs);
}

// Past the bound the window is still tried first: forcing is the fallback, not
// what the bound does.
TEST_CASE("durable write: past the bound a free phase is still preferred") {
    DurableWriteWindow window;
    window.request(0);
    const uint32_t t = DurableWriteWindow::kMaxDeferMs;
    REQUIRE(window.decide(anchored_plan(500), view_at(500, t), t) == DurableWriteVerdict::Forced);
    CHECK(window.decide(anchored_plan(50), view_at(50, t), t) == DurableWriteVerdict::Place);
}

// The bound is never reached in practice, because the second offers the window
// twice: whatever phase a change arrives at, the wait is under a second.
TEST_CASE("durable write: a change at any phase of the second is placed within the bound") {
    for (int start = 0; start < 1000; start += 5) {
        DurableWriteWindow window;
        const uint32_t requested_ms = static_cast<uint32_t>(60000 + start);
        window.request(requested_ms);
        uint32_t t = requested_ms;
        bool placed = false;
        for (int step = 0; step < 400 && !placed; step++) {
            t += 10;
            const int phase = static_cast<int>(t % 1000);
            const DurableWriteVerdict verdict =
                window.decide(anchored_plan(phase), view_at(phase, t), t);
            if (verdict == DurableWriteVerdict::Place) {
                window.placed(t, false);
                placed = true;
            }
            REQUIRE(verdict != DurableWriteVerdict::Forced);
        }
        REQUIRE(placed);
        CHECK(window.worst_wait_ms() < DurableWriteWindow::kMaxDeferMs);
        // The settle is the floor on the wait, and the window adds less than a
        // second on top of it.
        CHECK(window.worst_wait_ms() >= DurableWriteWindow::kSettleMs);
        CHECK(window.worst_wait_ms() <= DurableWriteWindow::kSettleMs + 1000);
    }
}

// M. The whole policy is unsigned differences from the first and last request, so
// the 49.7-day wrap of hal::Clock::millis() costs it nothing - but "costs it
// nothing" is a claim, and this is the case that holds it. A pilot stepping a
// setting through the wrap instant must not have their change deferred for seven
// weeks, and a dwell view stamped before the wrap must read as 84 ms old after it
// rather than as a lifetime.
TEST_CASE("durable write: the settle, the bound and the stale view span the 49.7-day wrap") {
    const uint32_t before = 0xFFFFFF00u;  // 256 ms short of the wrap

    DurableWriteWindow window;
    window.request(before);
    // Inside the settle, at a phase the write would otherwise fit: held.
    CHECK(window.decide(anchored_plan(50), view_at(50, before + 100u), before + 100u) ==
          DurableWriteVerdict::Hold);
    // The settle expires 750 ms after the request, which is 494 ms past zero.
    const uint32_t settled = before + DurableWriteWindow::kSettleMs;
    REQUIRE(settled < before);  // the case is worthless unless it wrapped
    CHECK(window.decide(anchored_plan(50), view_at(50, settled), settled) ==
          DurableWriteVerdict::Place);
    window.placed(settled, false);
    // 750 ms waited, not 49.7 days: worst_wait_ms is the same subtraction.
    CHECK(window.worst_wait_ms() == DurableWriteWindow::kSettleMs);

    // The bound, measured from a request made before the wrap and spent after it.
    DurableWriteWindow bound;
    bound.request(before);
    const uint32_t spent = before + DurableWriteWindow::kMaxDeferMs;
    CHECK(bound.decide(anchored_plan(500), view_at(500, spent), spent) ==
          DurableWriteVerdict::Forced);

    // The stamped view: asked at 20 ms past the wrap, about a dwell that published
    // its phase 64 ms before it.
    DurableWriteWindow view;
    const uint32_t asked = 20u;
    view.request(asked - 800u);
    const DwellPhase fresh = view_at(20, 0xFFFFFFC0u);   // 84 ms old
    const DwellPhase stale = view_at(20, 0xFFFFFF00u);   // 276 ms old, past the bound
    CHECK(view.decide(anchored_plan(104), fresh, asked) == DurableWriteVerdict::Place);
    CHECK(view.decide(anchored_plan(104), stale, asked) == DurableWriteVerdict::Hold);
}
