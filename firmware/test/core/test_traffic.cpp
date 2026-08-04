// The table is finite and the sky is not, so every entry that arrives asks which
// one leaves. That is a safety decision, not bookkeeping: an aircraft under alarm
// stays even when the table overflows, a second report of the same aircraft merges
// rather than doubles it, and a stale entry ages out instead of haunting the
// screen. The alarm cases pin the escalation as a target closes, and what the
// alarm is allowed to say out loud about a target it has already announced.
#include <cmath>

#include "core/traffic/alarm.h"
#include "core/traffic/link.h"
#include "core/traffic/sanity.h"
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

// A ground relay is a rebroadcast, so it is always the newer report and always
// the poorer one. Letting recency decide would walk a target we are hearing
// perfectly well backwards once a second, for as long as both paths last.
TEST_CASE("traffic: a relay does not displace a direct reception that is still fresh") {
    TrafficTable tbl;
    tbl.update(obs(0x111, 6, 100, messages::Source::AdslDirect), 100);
    const int idx = tbl.find(6, 0x111);
    REQUIRE(idx >= 0);

    for (uint32_t later = 101; later <= 100 + kDirectHoldSec; later++) {
        tbl.update(obs(0x111, 6, later, messages::Source::AdslUplink), later);
        CHECK(tbl.count() == 1);
        CHECK(tbl.at(idx)->obs.source == messages::Source::AdslDirect);
        CHECK(tbl.at(idx)->obs.rx_utc == 100);
    }

    // And the hold is a hold, not a block: past it the direct track is as stale
    // as the alarm layer's own patience with a contact, and the relay is the
    // only thing still reporting this aircraft.
    const uint32_t past = 100 + kDirectHoldSec + 1;
    tbl.update(obs(0x111, 6, past, messages::Source::AdslUplink), past);
    CHECK(tbl.count() == 1);
    CHECK(tbl.at(idx)->obs.source == messages::Source::AdslUplink);
    CHECK(tbl.at(idx)->obs.rx_utc == past);

    // A relay never blocks a target of its own, and a direct reception takes it
    // straight back.
    tbl.update(obs(0x222, 6, past, messages::Source::AdslUplink), past);
    CHECK(tbl.count() == 2);
    tbl.update(obs(0x111, 6, past, messages::Source::AdslDirect), past);
    CHECK(tbl.at(idx)->obs.source == messages::Source::AdslDirect);
}

// The hold is core/traffic/alarm.h's own freshness rule wearing a different
// unit. If one moves, the other has to, and this is what says so.
TEST_CASE("traffic: the direct hold is the alarm layer's patience with a contact") {
    CHECK(kDirectHoldSec * 1000 == kAlertMaxAgeMs);
}

// A ground station relays every aircraft it heard, and it heard us. Own-ship on
// the radar is a permanent collision with the aircraft the device is bolted to.
TEST_CASE("traffic: our own address is not traffic, whoever reports it") {
    TrafficTable tbl;
    tbl.set_own_address(0xC5D804);
    CHECK(tbl.update(obs(0xC5D804, 6, 100, messages::Source::AdslUplink), 100) < 0);
    CHECK(tbl.update(obs(0xC5D804, 6, 100, messages::Source::AdslDirect), 100) < 0);
    CHECK(tbl.count() == 0);
    CHECK(tbl.update(obs(0xC5D805, 6, 100, messages::Source::AdslUplink), 100) >= 0);
    CHECK(tbl.count() == 1);
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

// What the buzzer follows. notify says "say it now"; this says "and this is
// what still stands", which is the difference between a tone with a cadence and
// a tone nobody remembers to stop.
TEST_CASE("alarm: the announced level rises with the contact and falls only when it has") {
    AlarmTracker tracker;
    const messages::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    REQUIRE(tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t).assessment.level == 3);
    CHECK(int(tracker.announced_level(t)) == 3);

    // The contact opens out to the info ring. The tracker holds what it said
    // for a re-notification window, so a target sliding across a ring boundary
    // is not announced twice a second...
    t += 100;
    REQUIRE(tracker.update(own, neighbour(own, 2800, 0, 0, 20, 0, t), t).assessment.level == 1);
    CHECK(int(tracker.announced_level(t)) == 3);

    // ...and then it lets go, which is the moment the tone must change.
    for (int i = 0; i < 25; i++) {
        t += 100;
        tracker.update(own, neighbour(own, 2800, 0, 0, 20, 0, t), t);
    }
    CHECK(int(tracker.announced_level(t)) == 1);

    // A target nobody has heard from announces nothing at all: the same five
    // second window the reminders live in, so the buzzer stops when the sky
    // goes quiet rather than when the table finally forgets.
    CHECK(int(tracker.announced_level(t + kAlertMaxAgeMs + 1)) == 0);
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

// Decision 5.3, the same limitation as scenarios/circling_gaggle.json but with
// the radio and the fix stream taken out, so the numbers are arithmetic rather
// than a replay. Two gliders circle one thermal at 45 kt and 13 deg/s: a 102 m
// radius and a 28 s circle, which is what a glider climbing in a thermal flies
// (radius = v^2 / (g tan(bank)); 23.1 m/s at 29 deg of bank is 102 m, and
// core/traffic/alarm.h's own kCirclingTurnDps band already assumes it). Their
// cores are 75 m apart and they are 34 deg out of phase, so the separation
// breathes between 135 m and about 15 m once per circle while both fly a steady,
// correct, perfectly ordinary thermalling turn.
//
// What is pinned here is what the model does: it holds the pair at info through
// the whole convergence, because a pair matched in turn rate and direction
// inside kGaggleRangeM is suppressed on the strength of the turn match alone -
// there is nothing in the model that knows where either circle is centred. The
// same suppression is what makes the device usable in a gaggle at all. Separating
// the 15 m pass from the 135 m stand-off needs both curved paths projected
// forward, which is the v1.1 work, and the day it lands this case must alarm.
TEST_CASE("alarm: two gliders on offset circles converge to 15 m and stay at info") {
    AlarmTracker tracker;
    const double kPi = 3.14159265358979;
    const double radius_m = 102.0;
    const double own_centre_east_m = 102.0;
    const double target_centre_east_m = own_centre_east_m + 24.4;
    const double target_centre_north_m = 70.9;
    const int16_t turn_dps = 13;

    double min_separation_m = 1e9;
    uint32_t min_at_ms = 0;
    uint8_t level_at_min = 0;
    Suppression suppression_at_min = Suppression::None;
    uint8_t raw_peak_level = 0;

    for (int second = 0; second <= 20; second++) {
        const uint32_t t = 1000 + static_cast<uint32_t>(second) * 1000;
        const double own_phase = (270 + turn_dps * second) * kPi / 180.0;
        const double target_phase = (304 + turn_dps * second) * kPi / 180.0;
        const double own_east = own_centre_east_m + radius_m * std::sin(own_phase);
        const double own_north = radius_m * std::cos(own_phase);
        const double target_east = target_centre_east_m + radius_m * std::sin(target_phase);
        const double target_north = target_centre_north_m + radius_m * std::cos(target_phase);
        const double separation_m =
            std::sqrt((target_east - own_east) * (target_east - own_east) +
                      (target_north - own_north) * (target_north - own_north));

        const messages::OwnState own = flying(23, turn_dps * second, turn_dps);
        const messages::AircraftObs target =
            neighbour(own, static_cast<int>(target_north - own_north),
                      static_cast<int>(target_east - own_east), 0, 23, 34 + turn_dps * second, t);
        const uint8_t raw_level = assess(own, target).level;
        if (raw_level > raw_peak_level) raw_peak_level = raw_level;
        const AlarmTracker::Decision d = tracker.update(own, target, t);
        if (separation_m < min_separation_m) {
            min_separation_m = separation_m;
            min_at_ms = t;
            level_at_min = d.assessment.level;
            suppression_at_min = d.suppression;
        }
    }

    MESSAGE("closest approach " << min_separation_m << " m at t=" << min_at_ms << " ms, level "
                                << static_cast<int>(level_at_min));
    CHECK(min_separation_m < 20);
    CHECK(level_at_min == kSuppressedLevel);
    CHECK(suppression_at_min == Suppression::CoCircling);
    // And the geometry alone, with no memory of the turn, is no better: it grades
    // the same encounter urgent - at the far end of it as loudly as at the near.
    CHECK(raw_peak_level == 3);
}

// --- J. Range sanity on receive ---------------------------------------------
// Everything below the CRC has already passed. These cases are about what
// happens when the CRC was fooled: test/core/test_adsl.cpp counts silent
// miscorrections and the count is not zero, and a miscorrected position field
// decodes to a perfectly well-formed aircraft somewhere it cannot be.

TEST_CASE("traffic: a target further away than the radio can hear is a mis-decode") {
    const messages::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);

    // Ten kilometres out is a real contact on this band: it is well inside the
    // budget and it is what the radar's outer ring is for.
    messages::AircraftObs near_by = neighbour(own, 10000, 0, 0, 30, 180);
    near_by.addr = 0x4A0001;
    CHECK(tbl.update(near_by, 100) >= 0);

    // A hundred kilometres is not. Nothing at 14 dBm e.r.p. on 868 MHz reaches
    // this receiver from there, so the frame that said so was wrong.
    messages::AircraftObs ghost = neighbour(own, 100000, 0, 0, 30, 180);
    ghost.addr = 0x4A0002;
    CHECK(tbl.update(ghost, 100) < 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.implausible_count() == 1);

    // Straight up counts too: the altitude field miscorrects as readily as the
    // latitude one, and the slant range is the path the signal took.
    messages::AircraftObs high = neighbour(own, 0, 0, 60000, 0, 0);
    high.addr = 0x4A0003;
    CHECK(tbl.update(high, 100) < 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.implausible_count() == 2);
}

TEST_CASE("traffic: the plausibility gate is exact at its own boundary") {
    const messages::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);
    int32_t slant_m = 0;

    // On the limit is believed, past it is not. The integer geometry rounds, so
    // the boundary is approached from both sides with a metre of margin rather
    // than asserted on the exact metre.
    messages::AircraftObs on_limit = neighbour(own, kMaxPlausibleRangeM - 1, 0, 0, 30, 180);
    CHECK(range_check(own, on_limit, slant_m) == Plausibility::Believable);
    CHECK(slant_m <= kMaxPlausibleRangeM);

    messages::AircraftObs past_limit = neighbour(own, kMaxPlausibleRangeM + 100, 0, 0, 30, 180);
    CHECK(range_check(own, past_limit, slant_m) == Plausibility::TooFar);
    CHECK(slant_m > kMaxPlausibleRangeM);

    // And the threshold is the link budget, not a taste: 14 dBm e.r.p. against
    // about -107 dBm of sensitivity is 121 dB, and free space at 868.2 MHz has
    // spent it by the gate. One ring in from the gate the budget still holds,
    // which is what makes this a ceiling rather than a limit on what we display.
    CHECK(free_space_loss_db(kMaxPlausibleRangeM) >= 120);
    CHECK(free_space_loss_db(kMaxPlausibleRangeM / 2) < 121);
    // Four times the outermost thing the alarm layer will speak about, so no
    // contact a pilot could act on is inside the part being refused.
    CHECK(kMaxPlausibleRangeM > 4 * kInfoDistM);
}

// Without a fix there is no point to measure from. The gate says nothing rather
// than refusing everything: a device that has just booted still collects the
// traffic it hears, and the screen already knows it cannot place it.
TEST_CASE("traffic: with no fix of our own nothing is refused for being far away") {
    messages::OwnState own = flying(30, 0);
    own.fix_valid = false;
    TrafficTable tbl;
    tbl.set_own_reference(own);

    messages::AircraftObs far_away = neighbour(flying(30, 0), 100000, 0, 0, 30, 180);
    CHECK(tbl.update(far_away, 100) >= 0);
    CHECK(tbl.implausible_count() == 0);

    // Same for a report that carries no position at all: it is not a range claim,
    // so it is not this gate's business.
    int32_t slant_m = 0;
    messages::AircraftObs positionless = far_away;
    positionless.valid_pos = false;
    CHECK(range_check(flying(30, 0), positionless, slant_m) == Plausibility::NoReference);
}

// A table nobody told about own-ship gates nothing, which is the same behaviour
// as no fix: the gate is a refinement on the door, never a new way to be blind.
TEST_CASE("traffic: a table with no reference set behaves exactly as it did before") {
    TrafficTable tbl;
    messages::AircraftObs far_away = neighbour(flying(30, 0), 250000, 0, 0, 30, 180);
    CHECK(tbl.update(far_away, 100) >= 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.implausible_count() == 0);
}

// The uplink is the case that makes the gate per-observation rather than
// per-frame: one ground-station frame carries up to thirteen aircraft, and one
// ghost among them says nothing about the other twelve.
TEST_CASE("traffic: a ghost in a relayed frame does not take the good targets with it") {
    const messages::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);

    messages::AircraftObs good = neighbour(own, 2000, 500, 100, 30, 90);
    good.addr = 0x4B0001;
    good.source = messages::Source::AdslUplink;
    messages::AircraftObs ghost = neighbour(own, 400000, -200000, 0, 30, 270);
    ghost.addr = 0x4B0002;
    ghost.source = messages::Source::AdslUplink;
    messages::AircraftObs also_good = neighbour(own, -1500, 800, -200, 25, 180);
    also_good.addr = 0x4B0003;
    also_good.source = messages::Source::AdslUplink;

    CHECK(tbl.update(good, 100) >= 0);
    CHECK(tbl.update(ghost, 100) < 0);
    CHECK(tbl.update(also_good, 100) >= 0);
    CHECK(tbl.count() == 2);
    CHECK(tbl.find(6, 0x4B0001) >= 0);
    CHECK(tbl.find(6, 0x4B0002) == -1);
    CHECK(tbl.find(6, 0x4B0003) >= 0);
    CHECK(tbl.implausible_count() == 1);
}

// And a refused report never disturbs the aircraft it claims to be. The slot an
// aircraft holds is what the alarm layer is tracking, so a miscorrected frame
// carrying a known address must not move it, blank it or age it.
TEST_CASE("traffic: a mis-decode of a tracked aircraft does not move the aircraft") {
    const messages::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);

    messages::AircraftObs real_contact = neighbour(own, 1200, 0, 0, 30, 180, 100000);
    real_contact.addr = 0x4C0001;
    const int idx = tbl.update(real_contact, 100);
    REQUIRE(idx >= 0);
    tbl.at(idx)->alarm_level = 2;

    messages::AircraftObs same_aircraft_wrong_place = neighbour(own, 120000, 0, 0, 30, 180, 101000);
    same_aircraft_wrong_place.addr = 0x4C0001;
    CHECK(tbl.update(same_aircraft_wrong_place, 101) < 0);

    CHECK(tbl.count() == 1);
    CHECK(tbl.find(6, 0x4C0001) == idx);
    CHECK(tbl.at(idx)->obs.lat_1e7 == real_contact.lat_1e7);
    CHECK(tbl.at(idx)->obs.rx_utc == real_contact.rx_utc);
    CHECK(int(tbl.at(idx)->alarm_level) == 2);
}

// A relayed target crossed two links, so it is allowed to be further away than
// anything we could have heard for ourselves - and that is the whole point of the
// uplink. The same position from the same aircraft is a ghost on the direct path
// and a legitimate contact on the relayed one.
TEST_CASE("traffic: a relayed target is judged against the two hops it travelled") {
    const messages::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);
    int32_t slant_m = 0;

    messages::AircraftObs distant = neighbour(own, 45000, 0, 0, 30, 180);
    distant.addr = 0x4D0001;
    distant.source = messages::Source::AdslDirect;
    CHECK(range_check(own, distant, slant_m) == Plausibility::TooFar);
    CHECK(tbl.update(distant, 100) < 0);

    distant.source = messages::Source::AdslUplink;
    CHECK(range_check(own, distant, slant_m) == Plausibility::Believable);
    CHECK(tbl.update(distant, 100) >= 0);
    CHECK(tbl.count() == 1);

    // ALP-TAS is a direct reception like our own protocol: one hop, one ceiling.
    CHECK(plausible_range_m(messages::Source::Alptas) == kMaxPlausibleRangeM);
    CHECK(plausible_range_m(messages::Source::AdslDirect) == kMaxPlausibleRangeM);
    CHECK(plausible_range_m(messages::Source::AdslUplink) == kMaxRelayedRangeM);

    // Past the two-hop budget a relay is refused too: a ground station cannot
    // hand us an aircraft it could not have heard either.
    messages::AircraftObs relayed_ghost = neighbour(own, 200000, 0, 0, 30, 180);
    relayed_ghost.addr = 0x4D0002;
    relayed_ghost.source = messages::Source::AdslUplink;
    CHECK(tbl.update(relayed_ghost, 100) < 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.implausible_count() == 2);
}

// M. Two different clocks meet in this layer and only one of them wraps at
// 49.7 days. The table ages targets out on a SECONDS base (GNSS UTC when there is
// a fix, boot seconds when there is not) and the alarm tracker holds its own
// deadlines on hal::Clock::millis(). Both are unsigned differences, and these are
// the cases that keep them that way: a target must not be forgotten because the
// counter turned over, and a contact must not go unannounced for seven weeks.
TEST_CASE("traffic: the age-out is a difference, whichever side of the wrap the stamps fell") {
    TrafficTable tbl;
    // The seconds base a device with a UTC fix uses. 2^32 seconds is 136 years, so
    // the arithmetic below is the one that matters and it holds at any magnitude.
    const uint32_t utc = 0xFFFFFFF0u;
    tbl.update(obs(0x1, 6, utc), utc);
    tbl.update(obs(0x2, 6, utc + 20u), utc + 20u);
    // 25 s after the second report, which is 5 s past the seconds counter's own
    // end: the first is 45 s old and goes, the second is 25 s old and stays.
    const uint32_t later = utc + 45u;
    tbl.age_out(later, 30);
    CHECK(tbl.find(6, 0x1) == -1);
    CHECK(tbl.find(6, 0x2) >= 0);
    // And the eviction order is ages, not stamps: a table full of targets stamped
    // before the wrap still gives up its oldest to a newcomer stamped after it.
    TrafficTable full;
    for (uint32_t i = 0; i < static_cast<uint32_t>(TrafficTable::kCapacity); i++)
        REQUIRE(full.update(obs(0x2000u + i, 6, utc + i), utc + i) >= 0);
    const uint32_t newcomer = utc + 60u;  // after every stamp already in the table
    CHECK(full.update(obs(0x9999, 6, newcomer), newcomer) >= 0);
    CHECK(full.find(6, 0x2000) == -1);  // the oldest, and only it
    CHECK(full.find(6, 0x2001) >= 0);
}

TEST_CASE("alarm: a contact is announced and forgotten across the 49.7-day wrap") {
    AlarmTracker tracker;
    const messages::OwnState own = flying(30, 0);
    const uint32_t before = 0xFFFFF000u;  // 4096 ms short of the wrap

    // A head-on closing through the wrap instant: announced once, reminded at the
    // re-notification cadence, and the announced level stands while it is fresh.
    messages::AircraftObs target = neighbour(own, 400, 0, 0, 30, 180, 1000);
    REQUIRE(tracker.update(own, target, before).notify);
    CHECK(tracker.announced_level(before) == 3);

    // 3000 ms later, past the wrap. Still fresh (kAlertMaxAgeMs is 5000), so the
    // level still stands - an announced_level that read 0 here would be a buzzer
    // that stopped mid-alarm at the wrap.
    const uint32_t after = before + 3000u;
    target.rx_utc = 2;                                 // a new observation of the same aircraft
    CHECK(tracker.update(own, target, after).notify);  // the urgent reminder
    CHECK(tracker.announced_level(after) == 3);

    // Past the alert age with nothing new heard: no longer driving the annunciator.
    CHECK(tracker.announced_level(after + kAlertMaxAgeMs + 1u) == 0);
    // And past kForgetMs the slot is released, so the next aircraft can have it.
    tracker.forget_stale(after + kForgetMs + 1u);
    CHECK(tracker.target_turn_dps(target.addr_table, target.addr) == 0);
}
