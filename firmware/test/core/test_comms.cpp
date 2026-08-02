// core/comms config state machine tested over a link model with scripted JSON
// messages, NO device. Covers get, set-with-confirmation,
// the in-flight lockout (fail closed), backpressure and DFU routing.
//
// Two of these guard the wire the shell now provides: the gate is fed from the
// ADS-L flight code core/flight publishes, and a prompt standing on the panel
// is an open authorisation, so it has a life of its own that ends in a refusal.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "doctest/doctest.h"
#include "hal/dfu.h"
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

TEST_CASE("comms: confirm re-checks the gate, becoming airborne cancels apply") {
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

// The third way a device is asked to go dark (core/power/shutdown.h:
// ShutdownReason::LinkRequest). The button and the cutoff already had callers;
// this is the companion link's, behind the same gate as dfu and recovery.
TEST_CASE("comms: power_off is confirmed on the device, then latched for the sequencer") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);

    cs.on_rx(frame("{\"cmd\":\"power_off\"}"));
    CHECK(cs.pending() == Pending::PowerOff);
    CHECK_FALSE(cs.power_off_requested());  // a remote request alone turns nothing off
    CHECK(link.last().bytes.find("confirm_power_off") != std::string::npos);

    cs.confirm();
    CHECK(cs.pending() == Pending::None);
    CHECK(cs.power_off_requested());
    CHECK(link.last().bytes.find("\"ack\":true") != std::string::npos);

    cs.clear_power_off_request();
    CHECK_FALSE(cs.power_off_requested());
}

TEST_CASE("comms: power_off refused in flight") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"power_off\"}"));
    CHECK(cs.pending() == Pending::None);
    CHECK_FALSE(cs.power_off_requested());
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);

    // And a confirmation that arrives after takeoff does not turn it off either.
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"power_off\"}"));
    REQUIRE(cs.pending() == Pending::PowerOff);
    cs.set_flight_state(FlightState::Airborne);
    cs.confirm();
    CHECK_FALSE(cs.power_off_requested());
}

// D5: the reset reason was read at boot and shown on the self-test page, and a
// device in a field with a phone next to it has no self-test page in view.
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

// Wire one: the value behind the gate. Nothing used to set it, so flight_ stayed
// Unknown and every sensitive operation was refused forever - green tests, dead
// device. It now comes from state.own.flight_state, and only one code opens it.
TEST_CASE("comms: only the ADS-L on-ground code is permission, every other value refuses") {
    CHECK(flight_state_from(static_cast<uint8_t>(flight::FlightState::OnGround)) ==
          FlightState::Ground);
    CHECK(flight_state_from(static_cast<uint8_t>(flight::FlightState::Airborne)) ==
          FlightState::Airborne);
    CHECK(flight_state_from(static_cast<uint8_t>(flight::FlightState::Unknown)) ==
          FlightState::Unknown);

    // G.1.4 is two bits and we own neither the sender nor the future: a code
    // this build does not know is not a ground it may unlock on.
    for (uint16_t code = 3; code < 256; code++)
        CHECK(flight_state_from(static_cast<uint8_t>(code)) == FlightState::Unknown);

    // And the state machine behind it behaves exactly as it does when the value
    // is set by hand: a receiver that says "on the ground" is the only way in.
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(flight_state_from(0));
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    CHECK(cs.pending() == Pending::None);
    cs.set_flight_state(flight_state_from(1));
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    CHECK(cs.pending() == Pending::Dfu);
}

TEST_CASE("comms: a prompt nobody answers expires, and a later confirm grants nothing") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.tick(1000);

    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    REQUIRE(cs.pending() == Pending::Dfu);
    cs.tick(1000 + kConfirmWindowMs - 1);
    CHECK(cs.pending() == Pending::Dfu);

    cs.tick(1000 + kConfirmWindowMs);
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("expired") != std::string::npos);

    // The button pressed after the prompt came down authorises the operation
    // that was on it, or it authorises nothing. It is nothing.
    cs.confirm();
    CHECK_FALSE(cs.upload_allowed());
}

TEST_CASE("comms: taking off takes a standing prompt away with it") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"recovery\"}"));
    REQUIRE(cs.pending() == Pending::Recovery);

    cs.set_flight_state(FlightState::Airborne);
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
}

// The prompt is the whole security boundary, so it has to say what it is: a
// panel that shows an unlabelled question is a panel a pilot answers blind.
TEST_CASE("comms: every operation that needs authorising names itself and what it will do") {
    const Pending all[] = {Pending::Set, Pending::Dfu, Pending::Apply, Pending::Recovery,
                           Pending::PowerOff};
    for (Pending p : all) {
        CHECK(std::strlen(pending_title(p)) > 0);
        CHECK(std::strlen(pending_detail(p)) > 8);
    }
    // No two operations wear the same title, or confirming one would look like
    // confirming another.
    for (Pending a : all)
        for (Pending b : all)
            if (a != b) CHECK(std::strcmp(pending_title(a), pending_title(b)) != 0);

    CHECK(std::strlen(pending_title(Pending::None)) == 0);
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
