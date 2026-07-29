// The committed scenarios are regression fixtures: the same files the browser and
// the terminal load are replayed here, and a training scenario's expectations are
// the assertions. A bug found in flight becomes a file, not a bug report.
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
