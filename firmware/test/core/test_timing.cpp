#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"

using namespace skyblip::timing;

static ClockState anchored() { return ClockState{true, true, 0}; }

// Our cut of the second: two 400 ms M-band dwells, one channel each, so slot 1
// runs 800..1200 and its tail is the head of the next second. The uplink dwell
// is framed on the window our own ground station transmits in (210..390 ms, a
// burst that completes by 390). The M-band dwell opens at 400 to catch
// FLARM-generation traffic that starts around 405, earlier than the ADS-L direct
// slot our own bursts respect.
TEST_CASE("timing: dwell map matches the decided band split") {
    CHECK(Scheduler::state_at(0) == SlotState::Slot1);
    CHECK(Scheduler::state_at(199) == SlotState::Slot1);
    CHECK(Scheduler::state_at(200) == SlotState::SwitchMtoO);
    CHECK(Scheduler::state_at(204) == SlotState::SwitchMtoO);
    CHECK(Scheduler::state_at(205) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(390) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(394) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(395) == SlotState::SwitchOtoM);
    CHECK(Scheduler::state_at(399) == SlotState::SwitchOtoM);
    CHECK(Scheduler::state_at(400) == SlotState::Slot0);
    CHECK(Scheduler::state_at(798) == SlotState::Slot0);
    CHECK(Scheduler::state_at(799) == SlotState::Hop);
    CHECK(Scheduler::state_at(800) == SlotState::Slot1);
    CHECK(Scheduler::state_at(999) == SlotState::Slot1);
}

TEST_CASE("timing: every band edge keeps its guard") {
    CHECK(kUplinkRxStart <= kGroundEmitStart - kJitterGuardMs);
    CHECK(kUplinkRxEnd >= kGroundEmitEnd + kJitterGuardMs);
    // M->O after slot 1's tail, then O->M before slot 0.
    CHECK(kUplinkRxStart - kSlot1Wrap == kJitterGuardMs);
    CHECK(kSlot0Start - kUplinkRxEnd == kJitterGuardMs);
    // Two equal M-band dwells, one channel each.
    CHECK(kSlot0End - kSlot0Start == kSlot1End - kSlot1Start);
}

TEST_CASE("timing: band and channel per dwell") {
    CHECK(Scheduler::band_at(300) == Band::O);
    // The tail of slot 1: still M-band, still the second channel.
    CHECK(Scheduler::band_at(100) == Band::M);
    CHECK(Scheduler::freq_at(100) == kMband1Hz);
    CHECK(Scheduler::slot_of(100) == 1);
    CHECK(Scheduler::band_at(420) == Band::M);
    CHECK(Scheduler::band_at(500) == Band::M);
    CHECK(Scheduler::band_at(900) == Band::M);
    // §C.2: two M-band channels, one per direct half, so a receiver visits both.
    CHECK(Scheduler::freq_at(300) == kObandHz);
    CHECK(Scheduler::freq_at(420) == kMband0Hz);
    CHECK(Scheduler::freq_at(500) == kMband0Hz);
    CHECK(Scheduler::freq_at(900) == kMband1Hz);
}

TEST_CASE("timing: uplink window and direct slot predicates") {
    CHECK(Scheduler::in_uplink_rx(205));
    CHECK(Scheduler::in_uplink_rx(394));
    CHECK_FALSE(Scheduler::in_uplink_rx(395));
    // The dwells are wider than the direct slot at both ends: we listen from
    // 400 and to 1200, we transmit only inside 450..1000.
    CHECK_FALSE(Scheduler::in_direct_slot(420));
    CHECK(Scheduler::in_direct_slot(450));
    CHECK(Scheduler::in_direct_slot(999));
    CHECK_FALSE(Scheduler::in_direct_slot(100));
    CHECK_FALSE(Scheduler::in_direct_slot(300));
    CHECK(Scheduler::slot_of(420) == 0);
    CHECK(Scheduler::slot_of(500) == 0);
    CHECK(Scheduler::slot_of(900) == 1);
    CHECK(Scheduler::slot_of(300) == -1);
}

TEST_CASE("timing: a dwell stops early enough to retune before the next one") {
    Scheduler s;
    // The O->M edge is the safety-critical one: 5 ms of guard, then M-band at 400.
    CHECK(s.plan(300, anchored()).start_ms == kUplinkRxStart);
    CHECK(s.plan(300, anchored()).end_ms == kUplinkRxEnd);
    // The tail is the same dwell as 900 ms: one plan, 800..1200.
    CHECK(s.plan(100, anchored()).start_ms == kSlot1Start);
    CHECK(s.plan(100, anchored()).end_ms == kSlot1End);
    CHECK(s.plan(500, anchored()).start_ms == kSlot0Start);
    CHECK(s.plan(500, anchored()).end_ms == kSlot0End - kHopGuardMs);
    CHECK(s.plan(900, anchored()).end_ms == kSlot1End);
    CHECK(s.plan(900, anchored()).start_ms == kSlot1Start);
}

TEST_CASE("timing: TX only in M-band direct slots when clock is anchored") {
    Scheduler s;
    SlotPlan p = s.plan(500, anchored());
    CHECK(p.tx_allowed);
    CHECK(p.band == Band::M);
    CHECK_FALSE(p.listen_only);
    // The uplink window belongs to the ground infrastructure.
    p = s.plan(300, anchored());
    CHECK_FALSE(p.tx_allowed);
    CHECK(p.band == Band::O);
}

TEST_CASE("timing: PPS lost within holdover — still receiving, no slotted TX") {
    Scheduler s;
    ClockState c{true, false, 5000};
    SlotPlan p = s.plan(500, c);
    CHECK_FALSE(p.listen_only);
    CHECK_FALSE(p.tx_allowed);
}

TEST_CASE("timing: past holdover or no UTC — listen only, fail closed") {
    Scheduler s;
    ClockState past{true, false, kPpsHoldoverMs + 1};
    CHECK(s.plan(500, past).listen_only);
    CHECK_FALSE(s.plan(500, past).tx_allowed);

    ClockState no_utc{false, true, 0};
    CHECK(s.plan(500, no_utc).listen_only);
    CHECK_FALSE(s.plan(500, no_utc).tx_allowed);
}

namespace {

Transmitter airborne_transmitter(uint32_t addr = 0x0ABBCC) {
    Transmitter t;
    t.configure(addr);
    return t;
}

// The plan the scheduler hands the radio service for a phase inside a slot.
SlotPlan slot_plan(int phase_ms) {
    Scheduler s;
    return s.plan(phase_ms, anchored());
}

}  // namespace

TEST_CASE("transmit: the instant is inside the direct slot, with room for the burst") {
    Transmitter t = airborne_transmitter();
    int earliest = 1000, latest = 0;
    for (uint32_t utc = 1000; utc < 1500; utc++) {
        const Transmitter::Attempt a = t.attempt(slot_plan(500), utc, utc * 1000, true, 0);
        REQUIRE(a.go);
        // Not from kSlot0Start: the dwell opens at 400, the direct slot at 450.
        CHECK(a.at_ms >= kDirectStart);
        // The burst also has to end before the channel hop at 800, not just
        // before the direct slot ends at 1000.
        CHECK(a.at_ms + static_cast<int>(Transmitter::kAirTimeMs) +
                  Transmitter::kCompletionSlackMs <=
              kSlot0End);
        CHECK(a.freq_hz == kMband0Hz);
        CHECK_FALSE(a.force);
        if (a.at_ms < earliest) earliest = a.at_ms;
        if (a.at_ms > latest) latest = a.at_ms;
    }
    // The whole slot is usable: an open dwell is a tuned dwell, so 450 itself is
    // a legal instant and no guard is owed at the front.
    CHECK(earliest == kDirectStart);
    CHECK(latest ==
          kSlot0End - Transmitter::kCompletionSlackMs - static_cast<int>(Transmitter::kAirTimeMs));
}

// The upper channel's dwell opens at 800 already tuned - the hop guard before it
// is what paid for the retune - so 800 is its first legal instant.
TEST_CASE("transmit: the first instant of a dwell is the moment it opens") {
    Transmitter t = airborne_transmitter();
    t.sent(0, 0, false);
    int earliest = 2000, latest = 0;
    for (uint32_t utc = 1; utc < 500; utc++) {
        const Transmitter::Attempt a = t.attempt(slot_plan(900), utc, utc * 1000, true, 0);
        REQUIRE(a.go);
        if (a.at_ms < earliest) earliest = a.at_ms;
        if (a.at_ms > latest) latest = a.at_ms;
    }
    CHECK(earliest == kSlot1Start);
    CHECK(latest ==
          kDirectEnd - Transmitter::kCompletionSlackMs - static_cast<int>(Transmitter::kAirTimeMs));
}

TEST_CASE("transmit: two devices do not pick the same instant every second") {
    Transmitter a = airborne_transmitter(0x0ABBCC);
    Transmitter b = airborne_transmitter(0x123456);
    int same = 0;
    for (uint32_t utc = 0; utc < 100; utc++) {
        if (a.attempt(slot_plan(500), utc, utc * 1000, true, 0).at_ms ==
            b.attempt(slot_plan(500), utc, utc * 1000, true, 0).at_ms)
            same++;
    }
    CHECK(same < 5);
}

// §C.2.5: traffic alternates between the two M-band channels.
// The second dwell hears traffic past 1000 ms; the direct slot ends there, so
// our own burst never does.
TEST_CASE("transmit: the burst completes inside the direct slot, tail or no tail") {
    Transmitter t = airborne_transmitter();
    t.sent(0, 0, false);  // one transmission done, so the next one is the upper channel's
    for (uint32_t utc = 1; utc < 400; utc++) {
        const Transmitter::Attempt a = t.attempt(slot_plan(900), utc, utc * 1000, true, 0);
        REQUIRE(a.go);
        CHECK(a.freq_hz == kMband1Hz);
        CHECK(a.at_ms >= kSlot1Start);
        CHECK(a.at_ms + static_cast<int>(Transmitter::kAirTimeMs) +
                  Transmitter::kCompletionSlackMs <=
              kDirectEnd);
    }
    // And the tail is not a transmit opportunity, however anchored the clock is.
    CHECK_FALSE(t.attempt(slot_plan(100), 500, 500000, true, 0).go);
}

TEST_CASE("transmit: consecutive transmissions alternate channel and slot") {
    Transmitter t = airborne_transmitter();
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, 0).freq_hz == kMband0Hz);
    CHECK_FALSE(t.attempt(slot_plan(900), 10, 10000, true, 0).go);
    t.sent(10, 10500, false);
    CHECK_FALSE(t.attempt(slot_plan(500), 11, 11000, true, 0).go);
    CHECK(t.attempt(slot_plan(900), 11, 11000, true, 0).freq_hz == kMband1Hz);
}

// §G.1.16: at least 1 Hz airborne, 0.1 Hz on the ground.
TEST_CASE("transmit: one burst per second airborne, one per ten on the ground") {
    Transmitter t = airborne_transmitter();
    t.sent(10, 10500, false);
    CHECK_FALSE(t.attempt(slot_plan(900), 10, 10800, true, 0).go);
    CHECK(t.attempt(slot_plan(900), 11, 11000, true, 0).go);

    Transmitter g = airborne_transmitter();
    g.sent(10, 10500, false);
    CHECK_FALSE(g.attempt(slot_plan(900), 15, 15000, false, 0).go);
    CHECK(g.attempt(slot_plan(900), 21, 20600, false, 0).go);
}

// §G.1.16: the navigation solution must not be older than 500 ms.
TEST_CASE("transmit: a stale fix is not transmitted") {
    Transmitter t = airborne_transmitter();
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, 500).go);
    CHECK_FALSE(t.attempt(slot_plan(500), 10, 10000, true, 501).go);
}

// §D.3: force after 3000 ms of failed attempts, then 2000 ms off air.
TEST_CASE("transmit: a busy band forces a transmission, then goes quiet") {
    Transmitter t = airborne_transmitter();
    t.busy(10000);
    CHECK_FALSE(t.attempt(slot_plan(500), 12, 12999, true, 0).force);
    CHECK(t.attempt(slot_plan(500), 13, 13000, true, 0).force);

    t.sent(13, 13100, /*forced=*/true);
    CHECK_FALSE(t.attempt(slot_plan(900), 14, 14000, true, 0).go);
    CHECK_FALSE(t.attempt(slot_plan(900), 15, 15099, true, 0).go);
    CHECK(t.attempt(slot_plan(900), 15, 15100, true, 0).go);
}

TEST_CASE("transmit: nothing goes out unless the slot allows it") {
    Transmitter t = airborne_transmitter();
    Scheduler s;
    ClockState no_pps{true, false, 0};
    CHECK_FALSE(t.attempt(s.plan(500, no_pps), 10, 10000, true, 0).go);
    CHECK_FALSE(t.attempt(s.plan(300, anchored()), 10, 10000, true, 0).go);
    // Listening on M-band is not licence to transmit there yet.
    CHECK_FALSE(t.attempt(s.plan(420, anchored()), 10, 10000, true, 0).go);
}
