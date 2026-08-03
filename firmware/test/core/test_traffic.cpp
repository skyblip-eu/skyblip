// The table is finite and the sky is not, so every entry that arrives asks which
// one leaves. That is a safety decision, not bookkeeping: an aircraft under alarm
// stays even when the table overflows, a second report of the same aircraft merges
// rather than doubles it, and a stale entry ages out instead of haunting the
// screen. The alarm cases pin the escalation as a target closes, and what the
// alarm is allowed to say out loud about a target it has already announced.
#include <cmath>

#include "core/traffic/alarm.h"
#include "core/traffic/table.h"
#include "core/util/intmath.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

uint16_t c9(int deg) { return static_cast<uint16_t>(((deg % 360 + 360) % 360) * 512 / 360); }

messages::OwnState flying(int mps, int track_deg, int16_t turn_dps = 0) {
    messages::OwnState o{};
    o.turn_dps = turn_dps;
    o.fix_valid = true;
    o.lat_1e7 = 481000000;
    o.lon_1e7 = 81000000;
    o.alt_m = 1000;
    o.speed_q = static_cast<uint16_t>(mps * 4);
    o.track_c9 = c9(track_deg);
    return o;
}

// Placed by offset from own-ship, so a case reads as the picture out of the
// canopy rather than as two coordinates.
messages::AircraftObs neighbour(const messages::OwnState& own, int north_m, int east_m, int up_m,
                                int mps, int track_deg, uint32_t at_ms = 0) {
    messages::AircraftObs t{};
    t.addr = 0x314159;
    t.addr_table = 6;
    t.valid_pos = true;
    t.has_speed = true;
    t.speed_q = static_cast<uint16_t>(mps * 4);
    t.track_c9 = c9(track_deg);
    t.alt_m = own.alt_m + up_m;
    t.lat_1e7 = own.lat_1e7 + static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    const int16_t ang =
        static_cast<int16_t>((static_cast<int64_t>(own.lat_1e7) * 65536) / 3600000000LL);
    const int64_t east_scaled = (static_cast<int64_t>(east_m) << 14) / icos(ang);
    t.lon_1e7 = own.lon_1e7 + static_cast<int32_t>(east_scaled * 1000000 / 11132);
    t.rx_utc = at_ms / 1000;
    t.rx_ms = static_cast<uint16_t>(at_ms % 1000);
    return t;
}

}  // namespace

static messages::AircraftObs obs(uint32_t addr, uint8_t tbl, uint32_t t,
                                 messages::Source src = messages::Source::AdslDirect) {
    messages::AircraftObs o{};
    o.addr = addr;
    o.addr_table = tbl;
    o.rx_utc = t;
    o.source = src;
    o.valid_pos = true;
    o.lat_1e7 = 481000000;
    o.lon_1e7 = 81000000;
    o.alt_m = 1000;
    return o;
}

TEST_CASE("traffic: insert, find, count") {
    TrafficTable tbl;
    CHECK(tbl.count() == 0);
    int i = tbl.update(obs(0x111, 6, 100), 100);
    CHECK(i >= 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.find(6, 0x111) == i);
    CHECK(tbl.find(6, 0x222) == -1);
}

TEST_CASE("traffic: dedup merges the same target, fresher and direct win") {
    TrafficTable tbl;
    tbl.update(obs(0x111, 6, 100, messages::Source::AdslUplink), 100);
    // direct, same time -> should replace uplink (direct preferred)
    tbl.update(obs(0x111, 6, 100, messages::Source::AdslDirect), 100);
    CHECK(tbl.count() == 1);
    int idx = tbl.find(6, 0x111);
    CHECK(tbl.at(idx)->obs.source == messages::Source::AdslDirect);
    // older observation must not overwrite a fresher one
    tbl.update(obs(0x111, 6, 90, messages::Source::AdslUplink), 100);
    CHECK(tbl.at(idx)->obs.rx_utc == 100);
}

TEST_CASE("traffic: age-out removes stale entries") {
    TrafficTable tbl;
    tbl.update(obs(0x1, 6, 100), 100);
    tbl.update(obs(0x2, 6, 120), 120);
    tbl.age_out(140, 30);  // 0x1 is 40 s old -> gone; 0x2 is 20 s -> stays
    CHECK(tbl.find(6, 0x1) == -1);
    CHECK(tbl.find(6, 0x2) >= 0);
}

TEST_CASE("traffic: overflow drops oldest non-threat, keeps active alarms") {
    TrafficTable tbl;
    // fill capacity
    for (int i = 0; i < TrafficTable::kCapacity; i++) {
        int idx = tbl.update(obs(0x1000 + i, 6, 100 + i), 200);
        REQUIRE(idx >= 0);
    }
    // mark the oldest entry as an active alarm so it can't be evicted
    int oldest = tbl.find(6, 0x1000);
    tbl.at(oldest)->alarm_level = 3;
    int idx = tbl.update(obs(0x9999, 6, 300), 300);
    CHECK(idx >= 0);                  // newcomer placed
    CHECK(tbl.find(6, 0x1000) >= 0);  // protected alarm still present
}

TEST_CASE("alarm: level escalates as a target closes head-on") {
    messages::OwnState own{};
    own.fix_valid = true;
    own.lat_1e7 = 481000000;
    own.lon_1e7 = 81000000;
    own.alt_m = 1000;
    own.speed_q = 40 * 4;  // 40 m/s
    own.track_c9 = 0;      // north

    auto target_at = [&](int north_m, int up_m) {
        // Coming the other way at 40 m/s: 80 m/s of closure, which is the only
        // geometry the levels below were ever meant to describe.
        return neighbour(own, north_m, 0, up_m, 40, 180);
    };

    CHECK(assess(own, target_at(5000, 0)).level <= 1);  // far
    CHECK(assess(own, target_at(1200, 0)).level >= 2);  // important
    CHECK(assess(own, target_at(300, 0)).level == 3);   // urgent
    // large vertical separation suppresses the alarm
    CHECK(assess(own, target_at(300, 800)).level == 0);
}

TEST_CASE("alarm: invalid when own has no fix") {
    messages::OwnState own{};
    messages::AircraftObs t{};
    t.valid_pos = true;
    CHECK_FALSE(assess(own, t).valid);
}

// The bug this replaced added both speeds together whatever the geometry, so a
// neighbour running away from us was credited with everything it had. Four
// gliders drifting downwind in one thermal were a permanent level 3 that way,
// and a device that cries wolf every second gets switched off in the cockpit.
TEST_CASE("alarm: closing speed is the relative velocity on the line of sight") {
    const messages::OwnState own = flying(30, 0);

    // Ahead of us, going the same way at the same speed: the gap is not moving.
    const AlarmAssessment formation = assess(own, neighbour(own, 800, 0, 0, 30, 0));
    CHECK(formation.closing_mps == 0);

    // The same target turned around: both speeds, because both are spent on us.
    const AlarmAssessment head_on = assess(own, neighbour(own, 800, 0, 0, 30, 180));
    CHECK(head_on.closing_mps == doctest::Approx(60).epsilon(0.05));

    // Behind us and slower: the gap is opening at the difference.
    const AlarmAssessment overtaken = assess(own, neighbour(own, -800, 0, 0, 20, 0));
    CHECK(overtaken.closing_mps == doctest::Approx(-10).epsilon(0.15));

    // Abeam, flying parallel: nothing of that 30 m/s is aimed at us.
    const AlarmAssessment abeam = assess(own, neighbour(own, 0, 600, 0, 30, 0));
    CHECK(abeam.closing_mps == 0);
}

// A target inside the urgent ring that is running away is not urgent, and the
// one crossing our nose 2 km out at 80 m/s of closure is.
TEST_CASE("alarm: urgency is what the geometry says, not what the range ring says") {
    const messages::OwnState own = flying(30, 0);
    CHECK(assess(own, neighbour(own, 400, 0, 0, 30, 0)).level < 3);
    CHECK(assess(own, neighbour(own, -300, 0, 0, 40, 180)).level == 1);
    CHECK(assess(own, neighbour(own, 400, 0, 0, 30, 180)).level == 3);
    CHECK(assess(own, neighbour(own, 900, 0, 0, 50, 180)).level == 3);
}

// Uplinked and relayed traffic often arrives as a position with no velocity.
// Zero would make it the safest thing in the sky, which is a lie the alarm is
// not allowed to tell: what is unknown is charged at what these aircraft fly.
TEST_CASE("alarm: a target that reports no velocity degrades, it does not vanish") {
    const messages::OwnState own = flying(30, 0);
    messages::AircraftObs quiet = neighbour(own, 900, 0, 0, 0, 0);
    quiet.has_speed = false;

    const AlarmAssessment a = assess(own, quiet);
    CHECK(a.closing_mps >= 30 + kUnknownTargetSpeedMps - 1);
    CHECK(a.level == 3);
}

// The annunciator is not the alarm level: a target already announced at a level
// must not re-drive it every pass of the service loop. SoftRF keeps one
// notification per address (oss/SoftRF-lyusupov .../src/TrafficHelper.cpp:236-260).
TEST_CASE("alarm: a target is announced once per level, and again when it gets worse") {
    AlarmTracker tracker;
    const messages::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    AlarmTracker::Decision d = tracker.update(own, neighbour(own, 2800, 0, 0, 20, 180), t);
    REQUIRE(d.assessment.level == 1);
    CHECK(d.notify);
    CHECK(d.escalated);

    // Same target, same level, over and over: said once.
    for (int i = 0; i < 20; i++) {
        t += 100;
        d = tracker.update(own, neighbour(own, 2800, 0, 0, 20, 180, t), t);
        CHECK_FALSE(d.notify);
    }

    // Now it is important, and that is new information.
    t += 100;
    d = tracker.update(own, neighbour(own, 1200, 0, 0, 20, 180, t), t);
    CHECK(d.assessment.level >= 2);
    CHECK(d.notify);
    CHECK(d.escalated);
}

// Urgent is the one level that keeps talking, because it is the one asking the
// pilot to do something now. It repeats on SoftRF's cadence, not every tick.
TEST_CASE("alarm: an urgent contact says so again, at the re-notification cadence") {
    AlarmTracker tracker;
    const messages::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    AlarmTracker::Decision d = tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t);
    REQUIRE(d.assessment.level == 3);
    REQUIRE(d.notify);

    int spoken = 0;
    for (int i = 0; i < 50; i++) {
        t += 100;
        d = tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t);
        if (d.notify) spoken++;
    }
    CHECK(spoken == 2);
}

// A target we have not heard from is a memory, not a threat: it may have turned,
// landed or switched off. SoftRF alerts only inside ALERT_EXPIRATION_TIME.
TEST_CASE("alarm: a target that has gone quiet stops driving the annunciator") {
    AlarmTracker tracker;
    const messages::OwnState own = flying(30, 0);
    const messages::AircraftObs frozen = neighbour(own, 400, 0, 0, 30, 180, 1000);

    uint32_t t = 1000;
    REQUIRE(tracker.update(own, frozen, t).notify);

    int spoken = 0;
    for (int i = 0; i < 100; i++) {
        t += 200;
        if (tracker.update(own, frozen, t).notify) spoken++;
    }
    // Two reminders inside the five seconds it stayed fresh, then silence.
    CHECK(spoken == 2);
}

// Two gliders working the same core are close, co-altitude and converging by
// any straight-line model, several times a minute, for as long as the climb
// lasts. That is the case the pilot least wants an alarm for, and the one a
// range gate is loudest in.
TEST_CASE("alarm: two gliders circling the same thermal stop shouting at each other") {
    AlarmTracker tracker;
    const int16_t own_turn = 14;
    AlarmTracker::Decision d{};

    // Both on the same circle, a quarter turn apart and 20 m below: from the
    // turning frame the picture never changes, and it always shows the gap
    // shutting, which is exactly the trap.
    for (int i = 0; i < 6; i++) {
        const uint32_t t = 1000 + static_cast<uint32_t>(i) * 1000;
        const int track_deg = 90 + 15 * i;
        const messages::OwnState own = flying(25, track_deg, own_turn);
        const double bearing = (track_deg - 90) * 3.14159265358979 / 180.0;
        const int north_m = static_cast<int>(150 * std::cos(bearing));
        const int east_m = static_cast<int>(150 * std::sin(bearing));
        d = tracker.update(own, neighbour(own, north_m, east_m, 20, 25, track_deg + 160, t), t);
    }

    // The target's turn came out of its own reported track history, at the rate
    // our own bank says we are turning at.
    CHECK(tracker.target_turn_dps(6, 0x314159) >= kCirclingTurnDps);
    CHECK(d.assessment.closing_mps > 0);
    CHECK(d.suppression == Suppression::CoCircling);
    CHECK(d.assessment.level == kSuppressedLevel);
    CHECK_FALSE(d.notify);
}

// Suppressing a real head-on is worse than any nuisance alarm, so the thermal
// is not a quiet zone: an aircraft crossing it in a straight line is not
// circling with us, and nothing about our own bank suppresses it.
TEST_CASE("alarm: a head-on inside the thermal still alarms") {
    AlarmTracker tracker;
    AlarmTracker::Decision d{};

    for (int i = 0; i < 6; i++) {
        const uint32_t t = 1000 + static_cast<uint32_t>(i) * 1000;
        const messages::OwnState own = flying(25, 90 + 15 * i, 14);
        messages::AircraftObs intruder = neighbour(own, 400, 0, 0, 30, 180, t);
        intruder.addr = 0x777777;
        d = tracker.update(own, intruder, t);
    }

    CHECK(d.suppression == Suppression::None);
    CHECK(d.assessment.level == 3);
}

// A neighbour whose range has not moved for six solutions is not arriving,
// whatever ring it sits in - and the moment it turns towards us it is, on the
// very next fix, because that is the whole exposure this trade buys.
TEST_CASE("alarm: a neighbour holding station is quietened, and turning in undoes it") {
    AlarmTracker tracker;
    const messages::OwnState own = flying(30, 0);
    AlarmTracker::Decision d{};

    uint32_t t = 1000;
    d = tracker.update(own, neighbour(own, 400, 0, 0, 30, 0, t), t);
    CHECK(d.assessment.level == 2);
    CHECK(d.suppression == Suppression::None);

    for (int i = 0; i < 8; i++) {
        t += 1000;
        d = tracker.update(own, neighbour(own, 400, 0, 0, 30, 0, t), t);
    }
    CHECK(d.suppression == Suppression::SteadyRange);
    CHECK(d.assessment.level == kSuppressedLevel);

    t += 1000;
    d = tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t);
    CHECK(d.suppression == Suppression::None);
    CHECK(d.assessment.level == 3);
    CHECK(d.notify);
}
