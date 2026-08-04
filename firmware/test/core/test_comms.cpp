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
// G6's plug-in-and-read: the same on_rx dispatch that answers "status"
// answers "timing" from whatever core/timing::SlotTimingStats the device has
// been accumulating - no second channel, no panel real estate a bucket array
// would not fit on anyway.
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

// M. The two windows this service holds, across the 49.7-day wrap of
// hal::Clock::millis(). A ten-minute upload window that never closes is a device
// that will take firmware from a phone for seven weeks; a thirty-second prompt
// that expires the instant it is raised cannot be answered at all. Both are
// unsigned differences from a stamp guarded by a flag, and this is what says so.
TEST_CASE("comms: the upload and confirmation windows span the 49.7-day wrap") {
    const uint32_t before = 0xFFFFFF00u;  // 256 ms short of the wrap

    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.tick(before);
    cs.on_rx(frame("{\"cmd\":\"dfu\"}"));
    cs.confirm();
    REQUIRE(cs.upload_allowed());

    cs.tick(before + 9u * 60u * 1000u);  // nine minutes later, past the wrap
    CHECK(cs.upload_allowed());
    cs.tick(before + 11u * 60u * 1000u);
    CHECK_FALSE(cs.upload_allowed());

    // The prompt, on the same clock. Raised before the wrap, still standing after
    // it, and expired thirty seconds after it was raised.
    platform::host::Link second_link;
    ConfigService prompt(second_link, s);
    prompt.set_flight_state(FlightState::Ground);
    prompt.tick(before);
    prompt.on_rx(frame("{\"cmd\":\"dfu\"}"));
    REQUIRE(prompt.pending() == Pending::Dfu);
    prompt.tick(before + kConfirmWindowMs - 1u);
    CHECK(prompt.pending() == Pending::Dfu);
    prompt.tick(before + kConfirmWindowMs);
    CHECK(prompt.pending() == Pending::None);
    CHECK(second_link.last().bytes.find("expired") != std::string::npos);
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

// G1: the wire, not the gauge or the cutoff rule - core/power decided percent,
// charging and the level, comms only carries them to the tablet.
// The RED technical file asks for the clear-channel threshold in force and the
// interval it is assessed over, and asks for them as a test mode. This is not a
// mode: the same bench reply that carries the slot-timing histograms carries
// both, so the evidence is text in a laboratory's report. It rides with
// "timing" rather than with "status" because a pilot's tablet reads status on
// every battery step and none of this is for a pilot.

// E1. A dying cell must not corrupt the settings. NVS survives an interrupted
// write by design, but the sector it garbage-collects is the internal flash the
// running image executes from, and the moment a write lands is the moment a
// 22 dBm burst sags a 3.3 V cell. So below the low-battery warning the sector is
// not touched - and a companion app that patches a value per keystroke is told
// so, rather than being acknowledged for a write that will not happen.

TEST_CASE("comms: a set is refused below the low-battery warning, with the reason") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);

    power::BatteryState low{};
    low.valid = true;
    low.millivolts = 3400;
    cs.set_battery_state(low, power::PowerLevel::Low);
    CHECK_FALSE(cs.settings_writable());

    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":1}"));
    // Refused at the door: no prompt to walk over and confirm for a write that
    // was never going to happen.
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("low_power") != std::string::npos);
    CHECK(link.last().bytes.find("\"ack\":false") != std::string::npos);
    CHECK(int(s.aircraft_type) == 4);
    CHECK_FALSE(cs.settings_dirty());

    // A charger on the cable holds the terminal above the cell, so core/power
    // reports Normal again and the door opens.
    power::BatteryState charging{};
    charging.valid = true;
    charging.millivolts = 3400;
    charging.external_power = true;
    charging.charging = true;
    cs.set_battery_state(charging, power::PowerLevel::Normal);
    CHECK(cs.settings_writable());
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":1}"));
    REQUIRE(cs.pending() == Pending::Set);
    cs.confirm();
    CHECK(int(s.aircraft_type) == 1);
}

// The confirmation window is 30 s wide and a cell can cross the warning inside
// it. The gate is therefore asked twice, exactly as the flight-state gate is.
TEST_CASE("comms: a cell that falls while the prompt stands cancels the change") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":1}"));
    REQUIRE(cs.pending() == Pending::Set);

    power::BatteryState low{};
    low.valid = true;
    low.millivolts = 3100;
    cs.set_battery_state(low, power::PowerLevel::Cutoff);
    cs.confirm();
    CHECK(cs.pending() == Pending::None);
    CHECK(int(s.aircraft_type) == 4);
    CHECK_FALSE(cs.settings_dirty());
    CHECK(link.last().bytes.find("low_power") != std::string::npos);
}

// The power-failure comparator watches the SoC's own rail, which is at or below
// the cell: once it has fired, a healthy-looking divider reading is not the
// question any more.
TEST_CASE("comms: a fired power-failure comparator closes the door on its own") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);
    cs.set_flight_state(FlightState::Ground);
    power::BatteryState healthy{};
    healthy.valid = true;
    healthy.millivolts = 4000;
    cs.set_battery_state(healthy, power::PowerLevel::Normal);
    REQUIRE(cs.settings_writable());

    cs.set_supply_warned(true);
    CHECK_FALSE(cs.settings_writable());
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":1}"));
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("low_power") != std::string::npos);
}

// J. Die temperature in the status reply. The question it answers is one no other
// number on the device can: was this unit cooking. A canopy rail in August is
// 60 C of air over a black case, and the two failures that follow - a pack that
// will not charge, an e-paper panel that ghosts - both look like a fault in the
// part that gave up rather than in the afternoon that did it.

TEST_CASE("comms: the status reply carries the die temperature, in whole degrees") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);

    // Nothing has read the sensor yet: the key is absent, not zero.
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("die_temp_c") == std::string::npos);

    cs.set_die_temperature(412, /*valid=*/true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":41") != std::string::npos);
}

// A board with no sensor, a driver that refused a measurement and a device at
// freezing are three different things. 0.0 C is a plausible hangar morning, so a
// zero must never stand in for the first two.
TEST_CASE("comms: no reading is no key, never a zero") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);

    cs.set_die_temperature(0, /*valid=*/true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":0") != std::string::npos);

    cs.set_die_temperature(0, /*valid=*/false);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("die_temp_c") == std::string::npos);
}

TEST_CASE("comms: tenths are rounded away from zero on both sides of freezing") {
    platform::host::Link link;
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);

    // A cold morning must not read one degree warmer than it is, and a hot
    // afternoon must not read one cooler: -20.6 C is -21, +20.6 C is +21.
    cs.set_die_temperature(-206, true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":-21") != std::string::npos);

    cs.set_die_temperature(206, true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":21") != std::string::npos);

    cs.set_die_temperature(-4, true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":0") != std::string::npos);
}

// The ceiling test/core/test_link_payload.cpp holds for the whole dialect, asked
// again here for the one key that was added to the reply a phone is PUSHED: the
// widest device state there is, plus the widest temperature the driver will pass
// (its own gate is -50 to +125 C), inside the 182 bytes an iPhone carries.
TEST_CASE("comms: the status reply still fits the narrowest phone with the temperature on it") {
    for (const int16_t decicelsius : {int16_t(-500), int16_t(1250)}) {
        platform::host::Link link;
        link.declare_payload_bytes(kSmallestSupportedPayload);
        settings::Settings s = settings::defaults(1);
        ConfigService cs(link, s);
        cs.set_reset_reason(power::ResetReason::Lockup);
        cs.set_flight_state(FlightState::Airborne);
        power::BatteryState full{};
        full.millivolts = 4200;
        full.percent = 100;
        full.external_power = true;
        full.charging = true;
        full.valid = true;
        cs.set_battery_state(full, power::PowerLevel::Cutoff);
        cs.set_die_temperature(decicelsius, true);

        cs.on_rx(frame("{\"cmd\":\"status\"}"));
        REQUIRE(link.sent.size() == 1);
        const std::string body = link.last().bytes;
        CHECK(body.size() <= static_cast<size_t>(kSmallestSupportedPayload));
        CHECK(cs.link_drops() == 0);
        // Whole, not merely valid JSON: json::Writer drops a field rather than
        // cutting it, so the last key has to be there.
        CHECK(body.find("\"die_temp_c\":") != std::string::npos);
        CHECK(body.back() == '}');
    }
}

// J and L. The range gate's counter, and why it is not on the reply above: it is a
// ten-digit unsigned counter, the status reply has eleven bytes of headroom at its
// worst case, and a push that vanished whenever a unit was both hot and refusing
// packets would be the exact failure the payload ceiling exists to prevent. It
// reads out with the rest of the radio's counters instead, on the question that
// already asked for it.
TEST_CASE("comms: the range gate's refusals read out with the radio's own counters") {
    platform::host::Link link;
    link.declare_payload_bytes(kSmallestSupportedPayload);
    settings::Settings s = settings::defaults(1);
    ConfigService cs(link, s);

    Diagnostics& dump = cs.diagnostics();
    dump.refreshes = 1;
    dump.noise_dbm = -101;
    dump.rx_ok = 7;
    cs.set_range_refused(0);
    link.sent.clear();
    cs.on_rx(frame("{\"cmd\":\"radio\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last().bytes.find("\"cmd\":\"radio\"") != std::string::npos);
    CHECK(link.last().bytes.find("\"group\":\"radio\"") != std::string::npos);
    CHECK(link.last().bytes.find("\"range_refused\":0") != std::string::npos);
    CHECK(link.last().bytes.find("\"noise_dbm\":-101") != std::string::npos);
    CHECK(link.last().bytes.find("\"rx_ok\":7") != std::string::npos);

    // A counter never resets, so the widest it can be is the widest the dump can
    // carry: ten digits (test/core/test_diagnostics.cpp holds the ceiling and why).
    // Nine of those do not fit one notification an iPhone will accept, so the
    // answer is two whole frames rather than one short one.
    dump.gave_up = dump.rx_ok = dump.rx_bad = dump.tx_ok = dump.tx_busy = 2147483647u;
    cs.set_range_refused(2147483647u);
    link.sent.clear();
    cs.on_rx(frame("{\"cmd\":\"radio\"}"));
    std::string body;
    for (const platform::host::Link::Frame& f : link.sent) {
        CHECK(f.bytes.size() <= static_cast<size_t>(kSmallestSupportedPayload));
        CHECK(f.bytes.back() == '}');
        body += f.bytes;
    }
    CHECK(body.find("\"range_refused\":2147483647") != std::string::npos);
    CHECK(body.find("\"more\":false") != std::string::npos);
    CHECK(cs.link_drops() == 0);
}
