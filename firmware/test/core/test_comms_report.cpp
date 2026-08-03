// What the companion link REPORTS, as opposed to what it authorises: why the
// device came up, the state of the cell a pilot's tablet draws, and the bench
// numbers a laboratory copies into a compliance report. A link model and
// scripted JSON, no device, exactly as in test_comms.cpp - which keeps the
// authorisation state machine and nothing else.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {
messages::RxFrame frame(const char* json) {
    messages::RxFrame f{};
    f.session_id = 1;
    f.endpoint = messages::Endpoint::Config;
    f.len = static_cast<uint16_t>(std::strlen(json));
    std::memcpy(f.data.data(), json, f.len);
    return f;
}

power::BatteryState battery_of(uint8_t percent, bool charging, bool valid = true) {
    power::BatteryState b{};
    b.millivolts = 3700;
    b.percent = percent;
    b.external_power = charging;
    b.charging = charging;
    b.valid = valid;
    return b;
}
}  // namespace

TEST_CASE("comms: status reports why the device came up") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(0xAA55);
    std::memcpy(s.callsign, "D-KXYZ", 7);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.set_reset_reason(power::ResetReason::Watchdog);

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body.find("\"cmd\":\"status\"") != std::string::npos);
    CHECK(body.find("\"reset\":\"WATCHDOG\"") != std::string::npos);
    CHECK(body.find("\"flight\":\"ground\"") != std::string::npos);
    CHECK(body.find("D-KXYZ") != std::string::npos);

    // Unknown until the shell says otherwise, and never a stale answer.
    platform::host::Link fresh_link;
    ConfigService fresh(fresh_link, s);
    fresh.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(fresh_link.last().bytes.find("\"reset\":\"UNKNOWN\"") != std::string::npos);
}

TEST_CASE("comms: timing reports the accumulator's buckets and counters") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(0xAA55);
    timing::SlotTimingStats stats;
    stats.record_edge(0, true);
    stats.record_edge(1000000, true);  // one clean second: centre bucket
    stats.record_dwell_phase(200);     // inside the hop guard
    stats.record_missed();
    stats.record_refused();
    stats.record_refused();
    ConfigService cs(link, s, nullptr, &stats);

    cs.on_rx(frame("{\"cmd\":\"timing\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body.find("\"cmd\":\"timing\"") != std::string::npos);
    CHECK(body.find("\"pps_us\":\"0,0,0,1,0,0,0\"") != std::string::npos);
    CHECK(body.find("\"dwell_us\":\"0,0,0,0,1,0,0\"") != std::string::npos);
    CHECK(body.find("\"pps_samples\":1") != std::string::npos);
    CHECK(body.find("\"dwell_worst_us\":200") != std::string::npos);
    CHECK(body.find("\"holdover\":0") != std::string::npos);
    CHECK(body.find("\"missed\":1") != std::string::npos);
    CHECK(body.find("\"refused\":2") != std::string::npos);
}

TEST_CASE("comms: timing without an accumulator wired up says so, not zeros") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(0xAA55);
    ConfigService cs(link, s);
    cs.on_rx(frame("{\"cmd\":\"timing\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last().bytes.find("no_stats") != std::string::npos);
}

TEST_CASE("comms: status carries state of charge, the charging flag, the level and its validity") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.set_battery_state(battery_of(61, false), power::PowerLevel::Normal);

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body.find("\"battery_percent\":61") != std::string::npos);
    CHECK(body.find("\"charging\":false") != std::string::npos);
    CHECK(body.find("\"battery_valid\":true") != std::string::npos);
    CHECK(body.find("\"power_level\":\"OK\"") != std::string::npos);
}

TEST_CASE("comms: an invalid battery is reported as invalid, never as a false zero percent") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    // No set_battery_state call at all: no sample has ever arrived.

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    const std::string body = link.last().bytes;
    CHECK(body.find("\"battery_valid\":false") != std::string::npos);
    CHECK(body.find("\"battery_percent\":0") != std::string::npos);
    CHECK(body.find("\"power_level\":\"--\"") != std::string::npos);
}

TEST_CASE(
    "comms: no push without a link, one push per real change, none for a repeat or for noise "
    "inside a step") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);

    // A baseline, and then a real change, both before the link comes up.
    cs.set_battery_state(battery_of(50, false), power::PowerLevel::Normal);
    cs.set_battery_state(battery_of(50, true), power::PowerLevel::Normal);
    CHECK(link.sent.empty());

    cs.on_link_up(messages::LinkUp{1, 200});

    cs.set_battery_state(battery_of(50, true), power::PowerLevel::Normal);  // repeat: no push
    CHECK(link.sent.empty());

    cs.set_battery_state(battery_of(50, false), power::PowerLevel::Normal);  // charging flips back
    REQUIRE(link.sent.size() == 1);

    cs.set_battery_state(battery_of(50, false), power::PowerLevel::Normal);  // repeat: no storm
    CHECK(link.sent.size() == 1);

    cs.set_battery_state(battery_of(50, false), power::PowerLevel::Low);  // level changes
    CHECK(link.sent.size() == 2);

    cs.set_battery_state(battery_of(56, false), power::PowerLevel::Low);  // crosses a step (50->56)
    CHECK(link.sent.size() == 3);

    cs.set_battery_state(battery_of(57, false),
                         power::PowerLevel::Low);  // same step as 56: no push
    CHECK(link.sent.size() == 3);

    cs.on_link_down(messages::LinkDown{1});
    cs.set_battery_state(battery_of(90, false), power::PowerLevel::Normal);  // link is down again
    CHECK(link.sent.size() == 3);
}

TEST_CASE("comms: status has room for every worst-case field, and the last key survives whole") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(0xFFFFFF);
    std::memcpy(s.callsign, "ABCDEFGHI", 10);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Airborne);
    cs.set_reset_reason(power::ResetReason::Lockup);
    power::BatteryState battery{};
    battery.millivolts = 4200;
    battery.percent = 100;
    battery.external_power = true;
    battery.charging = true;
    battery.valid = true;
    cs.set_battery_state(battery, power::PowerLevel::Cutoff);

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    // Truncation would have dropped this whole last key, not cut it short.
    CHECK(body.substr(body.size() - 23) == "\"power_level\":\"CUTOFF\"}");
    CHECK(body.find("\"battery_percent\":100") != std::string::npos);
    CHECK(body.find("\"charging\":true") != std::string::npos);
    CHECK(body.find("\"battery_valid\":true") != std::string::npos);
}

TEST_CASE("comms: timing carries the carrier-sense threshold in force and its interval") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(0xAA55);
    timing::SlotTimingStats stats;
    ConfigService cs(link, s, nullptr, &stats);

    // Before the radio service has said anything, the cold-start threshold.
    cs.on_rx(frame("{\"cmd\":\"timing\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last().bytes.find("\"carrier_sense_dbm\":-95") != std::string::npos);

    cs.set_carrier_sense(timing::NoiseFloor::kThresholdCeilingDbm);
    cs.on_rx(frame("{\"cmd\":\"timing\"}"));
    const std::string body = link.last().bytes;
    CHECK(body.find("\"carrier_sense_dbm\":-82") != std::string::npos);
    // EN 300 220-2 V3.3.1 4.6.2.3 and 4.6.3.2, on the wire and not in a build
    // note: the ceiling nothing may escalate past, and the assessment interval.
    CHECK(body.find("\"carrier_sense_ceiling_dbm\":-82") != std::string::npos);
    CHECK(body.find("\"carrier_sense_us\":160") != std::string::npos);
    // The widest reply on this link, with every key whole.
    CHECK(body.back() == '}');
    CHECK(body.find("\"refused\":0") != std::string::npos);
}
