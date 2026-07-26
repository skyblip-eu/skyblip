#include "core/timing/slot.h"
#include "doctest/doctest.h"

using namespace skyblip::timing;

static ClockState anchored() { return ClockState{true, true, 0}; }

TEST_CASE("timing: state map matches the §0 slot table") {
    CHECK(Scheduler::state_at(210) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(394) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(396) == SlotState::SwitchOtoM);
    CHECK(Scheduler::state_at(400) == SlotState::Slot0);
    CHECK(Scheduler::state_at(799) == SlotState::Slot0);
    CHECK(Scheduler::state_at(800) == SlotState::Slot1);
    CHECK(Scheduler::state_at(999) == SlotState::Slot1);
    CHECK(Scheduler::state_at(100) == SlotState::SwitchMtoO);
}

TEST_CASE("timing: band assignment (O for uplink+return-guard, else M)") {
    CHECK(Scheduler::band_at(300) == Band::O);
    CHECK(Scheduler::band_at(100) == Band::O);  // returning to O
    CHECK(Scheduler::band_at(500) == Band::M);
    CHECK(Scheduler::band_at(900) == Band::M);
}

TEST_CASE("timing: uplink RX window and own TX slot predicates") {
    CHECK(Scheduler::in_uplink_rx(205));
    CHECK(Scheduler::in_uplink_rx(394));
    CHECK_FALSE(Scheduler::in_uplink_rx(395));
    CHECK(Scheduler::in_own_tx_slot(400));
    CHECK(Scheduler::in_own_tx_slot(850));
    CHECK_FALSE(Scheduler::in_own_tx_slot(300));
}

TEST_CASE("timing: TX only in M-band own slots when clock is anchored") {
    Scheduler s;
    SlotPlan p = s.plan(500, anchored());  // slot0
    CHECK(p.tx_allowed);
    CHECK(p.band == Band::M);
    CHECK_FALSE(p.listen_only);
    // O-band uplink window: air is RX-only, no TX
    p = s.plan(300, anchored());
    CHECK_FALSE(p.tx_allowed);
    CHECK(p.band == Band::O);
}

TEST_CASE("timing: PPS lost within holdover — M-band CSMA runs, slotted TX inhibited") {
    Scheduler s;
    ClockState c{true, false, 5000};  // utc known, pps lost 5 s ago
    SlotPlan p = s.plan(500, c);
    CHECK_FALSE(p.listen_only);  // still receiving on M
    CHECK_FALSE(p.tx_allowed);   // but no slotted own beacon
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

TEST_CASE("timing: guard is 5 ms on the safety-critical O->M edge") {
    // 395..400 is the switch window (5 ms).
    CHECK(kJitterGuardMs == 5);
    CHECK(Scheduler::state_at(395) == SlotState::SwitchOtoM);
    CHECK(Scheduler::state_at(399) == SlotState::SwitchOtoM);
    CHECK(Scheduler::state_at(400) == SlotState::Slot0);
}
