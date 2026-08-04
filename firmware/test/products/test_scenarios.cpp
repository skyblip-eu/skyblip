// The committed scenarios are regression fixtures: the same files the browser and
// the terminal load are replayed here, and a training scenario's expectations are
// the assertions. A bug found in flight becomes a file, not a bug report.
#include <string>

#include "core/protocol/nmea_out.h"
#include "core/traffic/alarm.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

const char* kInlineScenario =
    "{\"name\":\"inline\",\"mode\":\"training\",\"alt_m\":900,\"speed_kt\":50,"
    "\"track_deg\":90,\"duration_ms\":6000,"
    "\"aircraft\":[{\"north_m\":600,\"east_m\":200,\"up_m\":30,\"speed_mps\":40,"
    "\"track_deg\":200}],"
    "\"events\":[{\"at_ms\":3000,\"expect_traffic_min\":1},"
    "{\"at_ms\":5000,\"expect_alarm_min\":1},{\"at_ms\":5500,\"fix\":0}]}";

void replay(simulator::Simulator& s) {
    const uint32_t until = s.scenario().duration_ms == 0 ? 10000 : s.scenario().duration_ms;
    s.run(until);
}

}  // namespace

TEST_CASE("scenario: the parser reads ownship, traffic and events") {
    simulator::Scenario s;
    const int len = static_cast<int>(__builtin_strlen(kInlineScenario));
    REQUIRE(simulator::parse_scenario(kInlineScenario, len, s));
    CHECK(s.name == "inline");
    CHECK(s.mode == simulator::Mode::Training);
    CHECK(s.alt_m == 900);
    CHECK(s.track_deg == 90);
    CHECK(s.duration_ms == 6000);
    REQUIRE(s.aircraft.size() == 1);
    CHECK(s.aircraft[0].north_m == doctest::Approx(600));
    CHECK(s.aircraft[0].track_deg == doctest::Approx(200));
    REQUIRE(s.events.size() == 3);
    CHECK(s.events[0].kind == simulator::EventKind::ExpectTrafficMin);
    CHECK(s.events[1].kind == simulator::EventKind::ExpectAlarmMin);
    CHECK(s.events[2].kind == simulator::EventKind::Fix);
    CHECK(s.events[2].value == 0);
}

TEST_CASE("scenario: an unparseable scenario is refused, not half-applied") {
    simulator::Scenario s;
    CHECK_FALSE(simulator::parse_scenario(nullptr, 0, s));
    CHECK_FALSE(simulator::load_scenario("scenarios/does-not-exist.json", s));
}

TEST_CASE("scenario: a training scenario's expectations hold when replayed") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    simulator::Scenario sc;
    const int len = static_cast<int>(__builtin_strlen(kInlineScenario));
    REQUIRE(simulator::parse_scenario(kInlineScenario, len, sc));
    s.load(sc);
    replay(s);

    CHECK(s.world().failures() == 0);
    CHECK(s.product().state().rx_ok > 0);
    // The last event pulls the fix, and the firmware must follow it down.
    CHECK_FALSE(s.product().state().own.fix_valid);
}

TEST_CASE("scenario: the committed files replay with their expectations met") {
    for (const char* path :
         {"scenarios/head_on.json", "scenarios/gnss_loss.json", "scenarios/slot_timing.json"}) {
        simulator::Simulator s;
        REQUIRE(s.setup() == Status::Ok);
        REQUIRE_MESSAGE(s.load_file(path), path);
        replay(s);
        CHECK_MESSAGE(s.world().failures() == 0, path);
        CHECK_MESSAGE(s.product().state().traffic.count() >= 1, path);
    }
}

// The pinned-phase fixture is the slot map's regression test: two neighbours
// inside the direct dwells are heard, the one in the uplink window is not.
TEST_CASE("scenario: pinned transmit phases land where the dwell map says") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    REQUIRE(s.load_file("scenarios/slot_timing.json"));
    replay(s);
    CHECK(s.product().state().traffic.count() == 2);
    CHECK(s.world().air().deaf() > 0);
    CHECK(s.world().air().heard() > 0);
}

TEST_CASE("scenario: a failed expectation is reported, not swallowed") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    simulator::Scenario sc;
    sc.duration_ms = 3000;
    sc.events.push_back(simulator::ScenarioEvent{2000, simulator::EventKind::ExpectAlarmMin, 3});
    s.load(sc);
    replay(s);

    CHECK(s.world().failures() == 1);
    REQUIRE(s.world().first_failure() != nullptr);
}

// One dwell, two systems: an ADS-L neighbour and an ALP-TAS neighbour transmit on
// the same two M-band channels, and both have to arrive as targets. Losing the
// ALP-TAS one is the failure this fixture exists to catch, because it is silent:
// the radar simply shows less sky than there is.
TEST_CASE("scenario: ADS-L and ALP-TAS traffic are both heard in the same dwells") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    REQUIRE(s.load_file("scenarios/mixed_traffic.json"));
    replay(s);

    CHECK(s.world().failures() == 0);
    const traffic::TrafficTable& table = s.product().state().traffic;
    int adsl = 0, alptas = 0;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = table.at(i);
        if (t == nullptr || !t->used) continue;
        if (t->obs.source == messages::Source::AdslDirect) adsl++;
        if (t->obs.source == messages::Source::Alptas) alptas++;
    }
    CHECK(adsl == 1);
    CHECK(alptas == 1);
}

// The ALP-TAS target the scenario flies must come back with the position and
// identity it was sent with, not merely as a blip: a decrypt that lands on
// plausible-looking garbage is worse than a dropped frame.
TEST_CASE("scenario: an ALP-TAS target decodes to where it actually is") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    REQUIRE(s.load_file("scenarios/mixed_traffic.json"));
    s.run(5000);

    const traffic::TrafficTable& table = s.product().state().traffic;
    const messages::OwnState& own = s.product().state().own;
    int found = 0;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = table.at(i);
        if (t == nullptr || !t->used || t->obs.source != messages::Source::Alptas) continue;
        found++;
        // 900 m north and 250 m east of us at the start, closing from the south.
        const int64_t dlat = t->obs.lat_1e7 - own.lat_1e7;
        const int32_t north_m = static_cast<int32_t>((dlat * 11132) / 1000000);
        CHECK(north_m > 400);
        CHECK(north_m < 1100);
        CHECK(t->obs.alt_m < own.alt_m);
        // A FLARM-taxonomy address is reported as one, so an EFB labels it IDType 2.
        CHECK(protocol::addr_table_to_idtype(t->obs.addr_table) == 2);
        CHECK(t->obs.valid_pos);
    }
    CHECK(found == 1);
}

// The fixture for the way this device gets switched off in the cockpit: three
// gliders working the same thermal, co-altitude, a few hundred metres away, all
// going the same way at the same speed. Adding both speeds together called that
// 60 m/s of closure and held every one of them at "urgent" for as long as the
// climb lasted. They are traffic - they belong on the screen, they must not be
// an alarm - and the assertion is what the annunciator is allowed to say.
TEST_CASE("scenario: a gaggle in one thermal is traffic, not three collisions") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    REQUIRE(s.load_file("scenarios/gaggle.json"));
    replay(s);

    CHECK(s.world().failures() == 0);
    const traffic::TrafficTable& table = s.product().state().traffic;
    CHECK(table.count() == 3);
    CHECK(s.product().state().alarm_level <= traffic::kSuppressedLevel);
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = table.at(i);
        if (t == nullptr || !t->used) continue;
        CHECK(t->alarm_level <= traffic::kSuppressedLevel);
    }
}

// v1 ships the straight-line closure model and no circling prediction, which is a
// decision and not an oversight: the limitation is pinned here rather than argued
// in a meeting. The two fixtures below are the two sides of a thermal. What they
// measure is deliberately not what we wish happened.
//
// What one encounter did, read off the public surface: the alarm level the
// product published, the level the annunciator was last driven at, and the range
// core/traffic/alarm.h computed from the target's last report. One number comes
// from the world instead - how far apart the two aircraft really were - because
// a limitation stated in the device's own view of the range is not honest: that
// view is up to a second stale, and in a turn a second is 23 m.
namespace {

struct Encounter {
    static constexpr uint32_t kSampleMs = 100;
    // The tracker needs two reports from a target before it has a turn rate for
    // it, so everything before this is the model grading a stranger.
    static constexpr uint32_t kSettledMs = 5000;
    // Recognising the pair only lowers what the tracker GRADES it. What the ear
    // gets falls one re-notification window later, because a level the device
    // has already said out loud is only taken back after the contact has been
    // calmer than it for that whole window (traffic::notify_for).
    static constexpr uint32_t kQuietFromMs = kSettledMs + traffic::kRenotifyMs;

    bool reached[4]{};
    uint32_t first_ms[4]{};
    int32_t first_range_m[4]{};
    int32_t first_true_m[4]{};
    uint8_t last_spoken_level{0};
    uint32_t last_spoken_ms{0};
    // The loudest thing the pilot's ear got, either side of the settling point.
    // core/annunciation gives every level its own rhythm, so the buzzer is off
    // as often as it is on and the peak over time is the honest reading: a
    // single sample can land in the gap between two pulses of an urgent train.
    uint8_t spoken_peak_before_settle{0};
    uint8_t spoken_peak_after_settle{0};
    uint32_t last_spoken_above_info_ms{0};
    uint8_t settled_peak_level{0};
    int32_t min_true_m{999999};
    uint32_t min_true_ms{0};
    uint8_t level_at_min_true{0};
    int32_t min_reported_m{999999};
    int32_t traffic_count{0};
};

const traffic::Target* only_target(const bus::State& state) {
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = state.traffic.at(i);
        if (t != nullptr && t->used) return t;
    }
    return nullptr;
}

Encounter measure(simulator::Simulator& s) {
    Encounter e{};
    const uint32_t until = s.scenario().duration_ms;
    uint8_t spoken = 0;
    for (uint32_t t = 0; t <= until; t += simulator::Simulator::kStepMs) {
        s.step(t);

        // Every step, not every sample: the urgent train's pulses are 90 ms and
        // a 100 ms sample would step straight over them.
        if (s.alarm_level() != spoken) {
            spoken = s.alarm_level();
            if (spoken != 0) {
                e.last_spoken_level = spoken;
                e.last_spoken_ms = t;
                if (spoken > traffic::kSuppressedLevel) e.last_spoken_above_info_ms = t;
                uint8_t& peak = t >= Encounter::kQuietFromMs ? e.spoken_peak_after_settle
                                                             : e.spoken_peak_before_settle;
                if (spoken > peak) peak = spoken;
            }
        }

        if (t % Encounter::kSampleMs != 0) continue;

        const bus::State& state = s.product().state();
        const traffic::Target* target = only_target(state);
        if (target == nullptr) continue;

        const traffic::AlarmAssessment a = traffic::assess(state.own, target->obs);
        if (!a.valid) continue;
        const uint8_t level = state.alarm_level;
        const int32_t range_m = a.rel_dist_m;
        const int32_t true_m = static_cast<int32_t>(s.world().separation_m(0));

        if (level >= 1 && level <= 3 && !e.reached[level]) {
            e.reached[level] = true;
            e.first_ms[level] = t;
            e.first_range_m[level] = range_m;
            e.first_true_m[level] = true_m;
        }
        if (t >= Encounter::kSettledMs && level > e.settled_peak_level)
            e.settled_peak_level = level;
        if (range_m < e.min_reported_m) e.min_reported_m = range_m;
        if (true_m < e.min_true_m) {
            e.min_true_m = true_m;
            e.min_true_ms = t;
            e.level_at_min_true = level;
        }
        e.traffic_count = state.traffic.count();
    }
    return e;
}

void report(const Encounter& e) {
    MESSAGE("first important: t=" << e.first_ms[2] << " ms, model range=" << e.first_range_m[2]
                                  << " m, true separation=" << e.first_true_m[2] << " m");
    MESSAGE("first urgent:    t=" << e.first_ms[3] << " ms, model range=" << e.first_range_m[3]
                                  << " m, true separation=" << e.first_true_m[3] << " m");
    MESSAGE("last annunciated level " << static_cast<int>(e.last_spoken_level)
                                      << " at t=" << e.last_spoken_ms << " ms");
    MESSAGE("loudest annunciated: " << static_cast<int>(e.spoken_peak_before_settle) << " before "
                                    << Encounter::kQuietFromMs << " ms, "
                                    << static_cast<int>(e.spoken_peak_after_settle) << " after; "
                                    << "last above info at t=" << e.last_spoken_above_info_ms
                                    << " ms");
    MESSAGE("closest approach: " << e.min_true_m << " m true at t=" << e.min_true_ms
                                 << " ms, published level " << static_cast<int>(e.level_at_min_true)
                                 << ", closest the model ever saw " << e.min_reported_m << " m");
    MESSAGE("peak published level after " << Encounter::kSettledMs
                                          << " ms: " << static_cast<int>(e.settled_peak_level));
}

}  // namespace

// The committed limitation of the v1 alarm, and the reason decision 5.3 defers
// circling prediction rather than pretending it is not needed.
//
// Two gliders work one thermal on 200 m circles - 45 kt at 13 deg/s is a 102 m
// radius and a 28 s circle, the ordinary way a glider climbs, and the same band
// core/traffic/alarm.h already calls circling - but their cores are 75 m apart
// and they are a third of a circle out of phase. The separation therefore
// breathes between 135 m and 11 m once per circle: a real collision risk, flown
// every day, and invisible to a model that only knows range and range rate.
//
// Today's model says the wrong thing twice. For the first two and a half
// seconds, before the tracker has two reports to differentiate a turn rate out
// of, it grades the pair urgent at 130 m of true separation on 2 m/s of closure.
// From then on the co-circling suppression holds it at info, the buzzer goes
// quiet one re-notification window later, and both stay that way through the
// 11 m crossing at 13.9 s. The geometry-only grade underneath
// is no better: it is urgent for the whole approach and falls to info exactly at
// the near miss, because at the closest point the range rate is zero by
// definition.
//
// Nothing here is a bug to fix inside the straight-line model. Range and range
// rate cannot separate 11 m from 135 m in a gaggle; only projecting both curved
// paths can, which is v1.1.
TEST_CASE("scenario: two gliders sharing a thermal core pass inside 15 m in silence") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    REQUIRE(s.load_file("scenarios/circling_gaggle.json"));
    const Encounter e = measure(s);
    report(e);

    CHECK(s.world().failures() == 0);
    CHECK(e.traffic_count == 1);

    // The one alarm this encounter ever produced came before the pair was
    // recognised as co-circling, and it came at the far end of the breathing.
    CHECK(e.reached[3]);
    CHECK(e.first_ms[3] < Encounter::kSettledMs);
    CHECK(e.first_true_m[3] > 100);
    CHECK(e.spoken_peak_before_settle == 3);
    CHECK(e.last_spoken_above_info_ms < Encounter::kQuietFromMs);
    // Nothing louder than info, and in fact nothing at all: info is announced
    // once per escalation, so a level that stays at 1 is a buzzer that stays
    // quiet. The pilot hears one false urgent early and then silence through
    // the near miss.
    CHECK(e.spoken_peak_after_settle <= traffic::kSuppressedLevel);

    // And then silence, all the way through the near miss.
    CHECK(e.settled_peak_level == traffic::kSuppressedLevel);
    CHECK(e.min_true_m < 15);
    CHECK(e.min_true_ms > 10000);
    CHECK(e.level_at_min_true == traffic::kSuppressedLevel);

    // The device could not have known better from range alone either: a target
    // report is up to a second old, so at 23 m/s around a 102 m circle the
    // closest range the model was ever handed is more than twice the true miss.
    CHECK(e.min_reported_m > 20);
    CHECK(e.min_reported_m < 60);
    CHECK(e.min_reported_m > e.min_true_m * 2);
}

// The other side of the fence, so v1.1 cannot buy circling prediction by going
// deaf to the traffic v1 does catch: the same thermalling own-ship, and a glider
// arriving in a straight line at 45 m/s from 2.2 km out, co-altitude. Its track
// never matches ours, nothing suppresses it, and it must escalate all the way to
// urgent while there is still a kilometre in hand.
TEST_CASE("scenario: a glider joining the thermal on a straight line is still caught") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    REQUIRE(s.load_file("scenarios/thermal_joiner.json"));
    const Encounter e = measure(s);
    report(e);

    CHECK(s.world().failures() == 0);
    CHECK(e.traffic_count == 1);

    CHECK(e.reached[2]);
    CHECK(e.first_ms[2] < 22000);
    CHECK(e.first_range_m[2] > 1300);
    CHECK(e.reached[3]);
    CHECK(e.first_ms[3] < 30000);
    CHECK(e.first_range_m[3] > 900);
    CHECK(e.last_spoken_level == 3);

    // Own-ship's own circle swings the closure up and down under the target, so
    // the published level breathes with it. What is pinned is that the highest
    // thing it said is urgent and that it said it a kilometre out.
    CHECK(e.settled_peak_level == 3);
}

// The session boundary as a committed fixture: one file, replayed by the tests
// today and by the browser page the day someone wonders where the log came
// from. The device is stationary, then it flies, then it stops - and what the
// partition holds afterwards is exactly one flight with a beginning and an end.
TEST_CASE("scenario: a takeoff and a landing bracket one flight log session") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    REQUIRE(s.load_file("scenarios/takeoff_and_landing.json"));

    uint32_t parked_records = 0;
    uint32_t parked_writes = 0;
    uint32_t airborne_records = 0;
    bool was_recording = false;
    for (uint32_t t = 0; t <= 130000; t += simulator::Simulator::kStepMs) {
        s.step(t);
        if (t == 18000) {
            parked_records = s.product().flight_log().records_written();
            parked_writes = s.platform().log_flash().writes;
        }
        if (t == 70000) {
            was_recording = s.product().flight_log().recording();
            airborne_records = s.product().flight_log().records_written();
        }
    }

    // Twenty seconds parked: nothing written, no sector erased.
    CHECK(parked_records == 0);
    CHECK(parked_writes == 0);
    CHECK(was_recording);
    CHECK(airborne_records > 0);

    CHECK(s.world().failures() == 0);
    CHECK_FALSE(s.product().flight_log().recording());
    CHECK(s.product().flight_log().records_written() > airborne_records);
    CHECK(s.product().flight_log().sessions_on_flash() == 1);
}

// The ground relay as a committed fixture, so the case is reproducible in the
// browser and not only in a test file: three aircraft too far away for this
// device to hear, reaching it because a skyPost heard them and put all three in
// one O-band frame, alongside one neighbour it hears for itself on the M band.
//
// Before 2026-08-05 this scenario produced exactly one target. The uplink frame
// went to protocol::receive_mband, which frames the M band's two systems and
// nothing else, so it failed there and was counted as rx_bad: the feature was
// absent and its absence was indistinguishable from a noisy site.
TEST_CASE("scenario: a skyPost relay puts aircraft on the radar that we cannot hear") {
    simulator::Simulator s;
    REQUIRE(s.setup() == Status::Ok);
    REQUIRE(s.load_file("scenarios/ground_relay.json"));
    replay(s);

    CHECK(s.world().failures() == 0);
    const bus::State& state = s.product().state();

    int direct = 0, relayed = 0;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = state.traffic.at(i);
        if (t == nullptr || !t->used) continue;
        if (t->obs.source == messages::Source::AdslDirect) direct++;
        if (t->obs.source == messages::Source::AdslUplink) relayed++;
    }
    CHECK(direct == 1);
    CHECK(relayed == 3);

    // One frame a second, three aircraft in each, and none of it counted as an
    // M-band framing failure.
    CHECK(state.uplink_frames > 5);
    CHECK(state.uplink_bad == 0);
    CHECK(state.uplink_targets >= 3 * state.uplink_frames);
    CHECK(state.rx_ok > 0);

    // The tape says what it heard and where: the relay is on 869.525 and it is
    // read as a relay, not as an unframed burst.
    bool saw_uplink_line = false;
    for (int i = 0; i < s.world().air().record_count(); i++) {
        char line[128];
        s.world().air().format(i, line, sizeof(line));
        if (std::string(line).find("869.525 RX  ") != std::string::npos &&
            std::string(line).find("UPLINK 3 aircraft") != std::string::npos)
            saw_uplink_line = true;
    }
    CHECK(saw_uplink_line);
}
