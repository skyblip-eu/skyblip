// Airborne or not, from the fix stream. This is not a display value: it gates
// the DFU lockout and the transmit rate, so getting it wrong costs twice, and
// the two ways of getting it wrong are not symmetric. Declaring a takeoff that
// did not happen locks the update out on the ground; missing one leaves an
// aircraft transmitting at the rate a parked device uses. These cases are the
// two launches a speed threshold cannot tell apart: a glider on a ridge with
// the wind on the nose, and a glider being towed to the launch point.
#include "core/flight/state.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::flight;

namespace {

FlightSample solution(uint32_t at_ms, double mps, double climb_mps, int32_t alt_m = 500,
                      uint16_t hdop_e2 = 90) {
    FlightSample s{};
    s.at_ms = at_ms;
    s.speed_q = static_cast<uint16_t>(mps * 4);
    s.climb_e8 = static_cast<int16_t>(climb_mps * 8);
    s.climb_valid = true;
    s.alt_msl_m = alt_m;
    s.hdop_e2 = hdop_e2;
    s.fix_valid = true;
    return s;
}

// One second of solutions at a time, the cadence the receiver is configured for.
FlightState hold(FlightMonitor& monitor, uint32_t& at_ms, int seconds, double mps, double climb_mps,
                 int32_t alt_m = 500, uint16_t hdop_e2 = 90) {
    FlightState state = monitor.state();
    for (int i = 0; i < seconds; i++) {
        at_ms += 1000;
        state = monitor.update(solution(at_ms, mps, climb_mps, alt_m, hdop_e2));
    }
    return state;
}

}  // namespace

TEST_CASE("flight: a takeoff is five seconds of evidence, not one fast fix") {
    FlightMonitor monitor;
    uint32_t t = 0;
    REQUIRE(hold(monitor, t, 5, 0.0, 0.0) == FlightState::OnGround);

    // One solution at flying speed. A threshold comparison would have taken off
    // here, dropped the transmit rate back on the next fix, and repeated.
    t += 1000;
    CHECK(monitor.update(solution(t, 25.0, 0.0)) == FlightState::OnGround);
    t += 1000;
    CHECK(monitor.update(solution(t, 0.5, 0.0)) == FlightState::OnGround);

    // A roll that keeps going is a different thing, and it is allowed the same
    // five seconds before anybody acts on it.
    CHECK(hold(monitor, t, 4, 20.0, 0.0) == FlightState::OnGround);
    CHECK(hold(monitor, t, 2, 20.0, 1.0) == FlightState::Airborne);
}

// Wind on the nose at the ridge: the aircraft is flying, working, and its
// ground speed is a walking pace. The two-threshold comparison this replaced
// read it as parked - transmitting at the ground rate, update unlocked, in the
// air, next to other gliders doing the same thing.
TEST_CASE("flight: a ridge start with the wind on the nose is flying") {
    FlightMonitor monitor;
    uint32_t t = 0;
    REQUIRE(hold(monitor, t, 4, 0.0, 0.0) == FlightState::OnGround);

    // 3 m/s over the ground, 2.5 m/s up: no ground vehicle does the second half.
    CHECK(hold(monitor, t, 4, 3.0, 2.5) == FlightState::OnGround);
    CHECK(hold(monitor, t, 2, 3.0, 2.5) == FlightState::Airborne);
}

// A glider towed to the grid, a tug taxiing back, a trailer on the perimeter
// track: ground speed and nothing else. It stays on the ground even when the
// receiver hands us one wild solution in the middle of it.
TEST_CASE("flight: a taxi does not take off, and one bad solution does not either") {
    FlightMonitor monitor;
    uint32_t t = 0;
    REQUIRE(hold(monitor, t, 3, 0.0, 0.0) == FlightState::OnGround);
    CHECK(hold(monitor, t, 20, 3.0, 0.0) == FlightState::OnGround);

    t += 1000;
    CHECK(monitor.update(solution(t, 30.0, 0.0)) == FlightState::OnGround);
    CHECK(hold(monitor, t, 20, 3.0, 0.0) == FlightState::OnGround);
}

// Landing is the slow half on purpose: half the criterion, twice the hold, and
// a good solution repays the hold rather than wiping it, so a glider rolling
// out with the wing dropping and lifting still lands - and a winch launch's
// pause at the top does not.
TEST_CASE("flight: a landing is ten seconds of stillness, and a bounce does not undo it") {
    FlightMonitor monitor;
    uint32_t t = 0;
    REQUIRE(hold(monitor, t, 8, 25.0, 0.0) == FlightState::Airborne);

    CHECK(hold(monitor, t, 9, 0.5, 0.0) == FlightState::Airborne);
    CHECK(hold(monitor, t, 2, 0.5, 0.0) == FlightState::OnGround);

    REQUIRE(hold(monitor, t, 8, 25.0, 0.0) == FlightState::Airborne);
    for (int i = 0; i < 6; i++) {
        CHECK(hold(monitor, t, 2, 0.5, 0.0) == FlightState::Airborne);
        hold(monitor, t, 1, 3.0, 0.0);
    }
    CHECK(hold(monitor, t, 1, 0.5, 0.0) == FlightState::OnGround);
}

// Speed and climb are only worth what the fix behind them is worth, so the
// evidence is divided by the dilution of precision before it is believed
// (OGN divides by DOP above 1.0 for exactly this).
TEST_CASE("flight: a fix nobody should trust does not take off on its own") {
    FlightMonitor monitor;
    uint32_t t = 0;
    REQUIRE(hold(monitor, t, 3, 0.0, 0.0, 500, 3000) == FlightState::OnGround);
    CHECK(hold(monitor, t, 20, 4.5, 0.0, 500, 3000) == FlightState::OnGround);

    // The same movement on a fix worth trusting is a takeoff.
    CHECK(hold(monitor, t, 8, 4.5, 0.0, 500, 90) == FlightState::Airborne);
}

// Above two kilometres there is no ground to be on in the airspace this device
// flies in, whatever the receiver says about speed.
TEST_CASE("flight: altitude alone is proof of flight") {
    FlightMonitor monitor;
    uint32_t t = 0;
    REQUIRE(hold(monitor, t, 3, 0.0, 0.0) == FlightState::OnGround);
    CHECK(hold(monitor, t, 8, 0.0, 0.0, kAlwaysFlyingAltM + 500) == FlightState::Airborne);
    // And it holds the state up there: no landing at 2500 m.
    CHECK(hold(monitor, t, 20, 0.0, 0.0, kAlwaysFlyingAltM + 500) == FlightState::Airborne);
}

// The hold protects a transition. At the first solution there is nothing to
// protect, and a device rebooted in flight that says "unknown" for five seconds
// hands both the transmit rate and the update lockout their wrong default.
TEST_CASE("flight: a device switched on in the air says so at once") {
    FlightMonitor airborne;
    CHECK(airborne.update(solution(1000, 30.0, 0.0)) == FlightState::Airborne);

    FlightMonitor parked;
    CHECK(parked.update(solution(1000, 0.0, 0.0)) == FlightState::OnGround);
}

// Without a fix there is no claim to make, and ADS-L G.1.4 has a code that says
// so. What must not happen is forgetting: the aircraft is still where it was
// when the receiver went quiet.
TEST_CASE("flight: no fix is not a landing") {
    FlightMonitor monitor;
    uint32_t t = 0;
    REQUIRE(hold(monitor, t, 8, 30.0, 0.0) == FlightState::Airborne);

    FlightSample lost{};
    lost.at_ms = t + 1000;
    CHECK(monitor.update(lost) == FlightState::Unknown);
    CHECK(monitor.state() == FlightState::Airborne);

    t += 20000;
    CHECK(monitor.update(solution(t, 30.0, 0.0)) == FlightState::Airborne);
}

TEST_CASE("flight: the motion figure is speed plus four times the climb, DOP derated") {
    CHECK(motion_e8(solution(0, 4.0, 0.0)) == kTakeoffMotionE8);
    CHECK(motion_e8(solution(0, 0.0, 1.0)) == kTakeoffMotionE8);
    CHECK(motion_e8(solution(0, 0.0, -1.0)) == kTakeoffMotionE8);
    CHECK(motion_e8(solution(0, 4.0, 0.0, 500, 200)) == kTakeoffMotionE8 / 2);
    // A receiver that reports no DOP at all is not punished for it.
    CHECK(motion_e8(solution(0, 4.0, 0.0, 500, 0)) == kTakeoffMotionE8);
}

// M. The takeoff and landing holds are sums of unsigned differences between
// consecutive samples, so the 49.7-day wrap of hal::Clock::millis() is one
// ordinary second to this monitor. What it would cost if it were not: the gap
// across the wrap reads as 4.29 billion milliseconds, which is past
// kMaxSampleGapMs, so the sample is thrown away and a takeoff in progress loses
// its evidence - or worse, a landing declares itself in the air.
TEST_CASE("flight: the takeoff hold is counted across the 49.7-day wrap") {
    FlightMonitor monitor;
    uint32_t t = 0xFFFFF000u - 1000u;  // the roll starts 4096 ms before the wrap
    REQUIRE(hold(monitor, t, 3, 0.0, 0.0) == FlightState::OnGround);

    // Five seconds of evidence, straddling zero (the first sample of the roll is
    // refused as a speed jerk, as it is on any other day), and the sample that
    // crossed the wrap counted as one second like the others.
    CHECK(hold(monitor, t, 5, 20.0, 1.0) == FlightState::OnGround);
    CHECK(hold(monitor, t, 1, 20.0, 1.0) == FlightState::Airborne);

    // And the landing hold, ten seconds later, on the far side.
    CHECK(hold(monitor, t, 9, 0.0, 0.0) == FlightState::Airborne);
    CHECK(hold(monitor, t, 1, 0.0, 0.0) == FlightState::OnGround);
}
