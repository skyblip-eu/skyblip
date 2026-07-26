// core/comms config state machine tested over a fake link with scripted JSON
// messages, NO device (3-ARCHITECTURE §6/§8). Covers get, set-with-confirmation,
// the in-flight lockout (fail closed), backpressure and DFU routing.
#include <cstring>

#include "core/comms/config.h"
#include "devices/host/fake_link.h"
#include "doctest/doctest.h"
#include "hal/dfu.h"

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
struct SpyDfu : hal::Dfu {
    int triggered = 0;
    void trigger() override { triggered++; }
};
}  // namespace

TEST_CASE("comms: get returns current config on the Config endpoint") {
    host::FakeLink link;
    settings::Settings s = settings::defaults(0xAA55);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"get\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last_on(messages::Endpoint::Config));
    CHECK(link.last().bytes.find("config") != std::string::npos);
}

TEST_CASE("comms: set on the ground stages, needs confirmation, then applies") {
    host::FakeLink link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);

    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":1}"));
    CHECK(cs.pending() == Pending::Set);
    CHECK(link.last().bytes.find("confirm") != std::string::npos);
    CHECK(int(s.aircraft_type) == 4);  // not yet applied

    cs.confirm();
    CHECK(cs.pending() == Pending::None);
    CHECK(int(s.aircraft_type) == 1);  // applied
    CHECK(cs.settings_dirty());
    CHECK(link.last().bytes.find("\"ack\":true") != std::string::npos);
}

TEST_CASE("comms: set is REFUSED in flight (fail closed), no staging") {
    host::FakeLink link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":1}"));
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
    CHECK(int(s.aircraft_type) == 4);
}

TEST_CASE("comms: unknown flight-state refuses, and airborne latches") {
    host::FakeLink link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Unknown);  // never confirmed on ground
    CHECK(cs.flight_state() == FlightState::Unknown);
    cs.on_rx(frame("{\"cmd\":\"set\",\"stealth\":true}"));
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);

    // once airborne, a lost fix (Unknown) must NOT clear the latch
    cs.set_flight_state(FlightState::Airborne);
    cs.set_flight_state(FlightState::Unknown);
    CHECK(cs.flight_state() == FlightState::Airborne);
    // positive ground reading clears it
    cs.set_flight_state(FlightState::Ground);
    CHECK(cs.flight_state() == FlightState::Ground);
}

TEST_CASE("comms: confirm re-checks the gate — becoming airborne cancels apply") {
    host::FakeLink link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":1}"));
    cs.set_flight_state(FlightState::Airborne);  // took off before confirming
    cs.confirm();
    CHECK(int(s.aircraft_type) == 4);  // NOT applied
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
}

TEST_CASE("comms: START_DFU routed through confirmation, triggers hal::Dfu once") {
    host::FakeLink link;
    settings::Settings s = settings::defaults(1);
    SpyDfu dfu;
    ConfigService cs(link, s, &dfu);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    CHECK(cs.pending() == Pending::Dfu);
    CHECK(dfu.triggered == 0);
    cs.confirm();
    CHECK(dfu.triggered == 1);
}

TEST_CASE("comms: DFU refused in flight") {
    host::FakeLink link;
    settings::Settings s = settings::defaults(1);
    SpyDfu dfu;
    ConfigService cs(link, s, &dfu);
    cs.set_flight_state(FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    CHECK(cs.pending() == Pending::None);
    CHECK(dfu.triggered == 0);
}

TEST_CASE("comms: link down cancels a pending change") {
    host::FakeLink link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"set\",\"stealth\":true}"));
    CHECK(cs.pending() == Pending::Set);
    cs.on_link_down(messages::LinkDown{1});
    CHECK(cs.pending() == Pending::None);
}
