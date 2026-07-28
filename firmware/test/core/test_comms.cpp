// core/comms config state machine tested over a link model with scripted JSON
// messages, NO device. Covers get, set-with-confirmation,
// the in-flight lockout (fail closed), backpressure and DFU routing.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "hardware/platform/host/link.h"
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
    int confirmed = 0;
    int recovery = 0;
    void trigger() override { triggered++; }
    void confirm() override { confirmed++; }
    void enter_recovery() override { recovery++; }
};
}  // namespace

TEST_CASE("comms: get returns current config on the Config endpoint") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(0xAA55);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"get\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last_on(messages::Endpoint::Config));
    CHECK(link.last().bytes.find("config") != std::string::npos);
}

TEST_CASE("comms: set on the ground stages, needs confirmation, then applies") {
    platform::host::Link link;
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
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":1}"));
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
    CHECK(int(s.aircraft_type) == 4);
}

TEST_CASE("comms: unknown flight-state refuses, and airborne latches") {
    platform::host::Link link;
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
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":1}"));
    cs.set_flight_state(FlightState::Airborne);  // took off before confirming
    cs.confirm();
    CHECK(int(s.aircraft_type) == 4);  // NOT applied
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
}

// "dfu" no longer reboots: under MCUmgr the image arrives over SMP afterwards,
// so confirming opens a write window instead. The reboot is the client's
// subsequent `os reset`, or an explicit "apply".
TEST_CASE("comms: dfu opens an upload window only after on-screen confirmation") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    SpyDfu dfu;
    ConfigService cs(link, s, &dfu);
    cs.set_flight_state(FlightState::Ground);

    CHECK_FALSE(cs.upload_allowed());
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    CHECK(cs.pending() == Pending::Dfu);
    CHECK_FALSE(cs.upload_allowed());  // a remote request alone authorises nothing

    cs.confirm();
    CHECK(cs.upload_allowed());
    CHECK(dfu.triggered == 0);
}

TEST_CASE("comms: apply routed through confirmation, triggers hal::Dfu once") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    SpyDfu dfu;
    ConfigService cs(link, s, &dfu);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"apply\"}"));
    CHECK(cs.pending() == Pending::Apply);
    CHECK(dfu.triggered == 0);
    cs.confirm();
    CHECK(dfu.triggered == 1);
}

TEST_CASE("comms: recovery reboots into the drag-and-drop bootloader after confirm") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    SpyDfu dfu;
    ConfigService cs(link, s, &dfu);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"recovery\"}"));
    CHECK(cs.pending() == Pending::Recovery);
    CHECK(dfu.recovery == 0);
    cs.confirm();
    CHECK(dfu.recovery == 1);
}

TEST_CASE("comms: recovery refused in flight") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    SpyDfu dfu;
    ConfigService cs(link, s, &dfu);
    cs.set_flight_state(FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"recovery\"}"));
    CHECK(cs.pending() == Pending::None);
    CHECK(dfu.recovery == 0);
}

// Fail closed: takeoff must revoke an authorisation granted on the ground, or a
// long upload could still be running when the aircraft leaves.
TEST_CASE("comms: takeoff closes an open upload window and it stays latched") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    cs.confirm();
    REQUIRE(cs.upload_allowed());

    cs.set_flight_state(FlightState::Airborne);
    CHECK_FALSE(cs.upload_allowed());

    // An "Unknown" reading after takeoff must not read as permission.
    cs.set_flight_state(FlightState::Unknown);
    CHECK_FALSE(cs.upload_allowed());
}

TEST_CASE("comms: upload window expires") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.tick(1000);
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    cs.confirm();
    REQUIRE(cs.upload_allowed());

    cs.tick(1000 + 9u * 60u * 1000u);
    CHECK(cs.upload_allowed());
    cs.tick(1000 + 11u * 60u * 1000u);
    CHECK_FALSE(cs.upload_allowed());
}

TEST_CASE("comms: disconnect closes the upload window") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    cs.confirm();
    REQUIRE(cs.upload_allowed());

    messages::LinkDown down{};
    cs.on_link_down(down);
    CHECK_FALSE(cs.upload_allowed());
}

TEST_CASE("comms: DFU refused in flight") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    SpyDfu dfu;
    ConfigService cs(link, s, &dfu);
    cs.set_flight_state(FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    CHECK(cs.pending() == Pending::None);
    CHECK(dfu.triggered == 0);
}

TEST_CASE("comms: link down cancels a pending change") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"set\",\"stealth\":true}"));
    CHECK(cs.pending() == Pending::Set);
    cs.on_link_down(messages::LinkDown{1});
    CHECK(cs.pending() == Pending::None);
}
