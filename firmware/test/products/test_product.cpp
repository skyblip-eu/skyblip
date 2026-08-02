// The acceptance invariant on the host: the real product (board, services,
// drivers) links and runs on the host platform with zero framework code. If any
// of it leaks a Zephyr include, this stops compiling.
#include <cstring>
#include <string>

#include "core/flight/atmosphere.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"
#include "ui/screens/boot.h"
#include "ui/widgets/wordmark.h"

using namespace skyblip;

namespace {

using Go = go::Product<platform::host::Platform>;

struct Rig {
    platform::host::Platform platform;
    Go product{platform};

    explicit Rig(hal::Capabilities fitted = platform::host::Platform::kFullyFitted)
        : platform(fitted) {}

    Status setup() { return product.setup(); }

    void run(uint32_t from, uint32_t to, uint32_t step = 50) {
        for (uint32_t t = from; t <= to; t += step) {
            platform.clock().set_millis(t);
            product.step(t);
        }
    }

    void push_fix(int32_t alt_m, uint32_t updates) {
        gnss::GnssFix f{};
        f.valid = true;
        f.alt_m = alt_m;
        f.updates = updates;
        product.bus().gnss.push(f);
    }

    void push_baro(int32_t alt_cm, uint32_t at_ms) {
        product.bus().baro.push(messages::BaroSample{flight::alt_cm_to_pressure(alt_cm), at_ms});
    }

    // Held across steps, then released across steps: ui::Button only reports a
    // press once a level has been stable through its debounce window.
    void press(uint32_t& t) {
        platform.board_gpio().button_down = true;
        hold(t);
        platform.board_gpio().button_down = false;
        hold(t);
    }

    void hold(uint32_t& t) {
        for (int i = 0; i < 2; i++) {
            platform.clock().set_millis(t);
            product.step(t);
            t += 40;
        }
    }

    bus::State& state() { return product.state(); }

    // The companion app's side of the link, arriving where the board polls it.
    void send(const char* json) {
        messages::RxFrame frame{};
        frame.endpoint = messages::Endpoint::Config;
        frame.len = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.len);
        platform.link().push_rx(frame);
    }
};

// A board with no fitted barometer: the samples in these cases are pushed by
// hand, so the board must not also be pumping its own.
constexpr hal::Capabilities kBaroByHand =
    static_cast<hal::Capabilities>(static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
                                   ~static_cast<uint32_t>(hal::Capability::Baro));

}  // namespace

TEST_CASE("product: setup brings the radio to Rx and reports its capabilities") {
    Rig rig;
    CHECK(rig.setup() == Status::Ok);
    CHECK(rig.state().started);
    CHECK(hal::has(rig.product.capabilities(), hal::Capability::Rf | hal::Capability::Gnss));
    CHECK(rig.product.degraded() == hal::Capability::None);
    CHECK(rig.state().settings.device_addr == 0x0ABBCC);
}

TEST_CASE("product: a missing optional capability is degraded, a required one refuses") {
    constexpr hal::Capabilities kNoBaro = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Baro));
    Rig degraded(kNoBaro);
    CHECK(degraded.setup() == Status::Ok);
    CHECK(degraded.product.degraded() == hal::Capability::Baro);

    constexpr hal::Capabilities kNoGnss = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Gnss));
    Rig refused(kNoGnss);
    CHECK(refused.setup() == Status::Down);
    CHECK_FALSE(refused.state().started);
}

TEST_CASE("product: the radio executor is armed against slot deadlines, not polled") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    const uint32_t before = rig.product.radio().arm_count();
    rig.run(0, 3000);
    // Two band changes per UTC second: O-band uplink dwell, then the M-band slots.
    CHECK(rig.product.radio().arm_count() >= before + 4);
}

TEST_CASE("product: step() runs the service cycle deterministically under a modelled clock") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 3000);
    uint8_t pkt[6] = {1, 2, 3, 4, 5, 6};
    rig.platform.chips().radio.queue_rx(pkt, sizeof(pkt));
    rig.run(3000, 4200);
    CHECK(rig.state().rx_bad >= 1);  // too short to be ADS-L: counted, never shown
    CHECK(rig.state().traffic.count() == 0);
}

TEST_CASE("product: the e-paper refreshes on change, not on cadence") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 5000);
    // The self-test page setup() paints, then the first radar frame, both full.
    // The sky then stays static, so nothing else reaches the glass.
    CHECK(rig.platform.chips().epd.present_count == 2);
    CHECK(rig.platform.chips().epd.last_full);

    rig.push_fix(1000, 1);  // fix arrives: the radar page changes
    rig.run(5000, 8000);
    CHECK(rig.platform.chips().epd.present_count == 3);
    CHECK_FALSE(rig.platform.chips().epd.last_full);  // differential, no flash
}

TEST_CASE("product: a button press switches page and the layout swap lands full") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.screen().page() == go::Page::Radar);

    uint32_t t = 100;
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::SixPack);
    rig.run(t, t + 4000);  // panel settles, floor passes: the page-change full lands
    t += 4000;
    CHECK(rig.platform.chips().epd.last_full);
    CHECK(rig.product.screen().fasts_since_full() == 0);

    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Status);
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Signal);

    // Every page is on the rotation by default, and the rotation closes.
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Radar);
}

TEST_CASE("product: page_mask disables pages so the button skips them") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.state().settings.page_mask = 0x05;  // radar + status only
    uint32_t t = 100;
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Status);
    rig.press(t);
    CHECK(rig.product.screen().page() == go::Page::Radar);
}

TEST_CASE("product: persisted settings are loaded on setup") {
    Rig rig;
    settings::Settings s = settings::defaults(0x223344);
    s.alarm_volume = 1;
    uint8_t blob[64];
    settings::to_blob(s, blob, sizeof(blob));
    REQUIRE(rig.platform.kv().write("settings", blob, settings::blob_size()) == Status::Ok);

    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.state().settings.alarm_volume == 1);
    CHECK(rig.state().settings.device_addr == 0x223344);
}

TEST_CASE("product: barometric pressure drives vertical speed") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(rig.product.ownship().baro_active());

    rig.push_baro(100000, 1000);
    rig.run(1000, 1000);
    CHECK(rig.product.ownship().baro_active());
    rig.push_baro(101000, 3000);
    rig.run(3000, 3000);

    // +10 m in 2 s = +5 m/s = 40 eighth-m/s.
    CHECK(rig.state().own.climb_e8 == doctest::Approx(40).epsilon(0.05));
}

TEST_CASE("product: a baro sample inside the window is ignored, not extrapolated") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_baro(100000, 1000);
    rig.run(1000, 1000);
    rig.push_baro(199000, 1100);
    rig.run(1100, 1100);
    CHECK(rig.state().own.climb_e8 == 0);
}

TEST_CASE("product: with no barometer, vertical speed comes from GNSS") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);

    rig.push_fix(1000, 1);
    rig.run(1000, 1000);
    rig.push_fix(1010, 2);
    rig.run(3000, 3000);

    CHECK_FALSE(rig.product.ownship().baro_active());
    CHECK(rig.state().own.climb_e8 == doctest::Approx(40).epsilon(0.05));
}

TEST_CASE("product: once the barometer speaks, GNSS stops setting vertical speed") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);

    rig.push_baro(100000, 500);
    rig.run(500, 500);
    rig.push_baro(100200, 1500);  // +2 m in 1 s = +16 e8
    rig.run(1500, 1500);
    const int16_t from_baro = rig.state().own.climb_e8;
    CHECK(from_baro == doctest::Approx(16).epsilon(0.1));

    rig.push_fix(1000, 1);
    rig.run(2000, 2000);
    rig.push_fix(1200, 2);
    rig.run(4000, 4000);

    CHECK(rig.state().own.climb_e8 == from_baro);
}

TEST_CASE("product: the board reads the cell and the gauge publishes it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(rig.state().battery.valid);

    rig.platform.battery().millivolts = 3800;
    rig.run(0, 12000);
    CHECK(rig.state().battery.valid);
    CHECK(rig.state().battery.millivolts == 3800);
    CHECK(rig.state().battery.percent == power::percent_from_mv(3800, false));
    CHECK_FALSE(rig.state().battery.charging);
    CHECK_FALSE(rig.state().battery.external_power);
}

TEST_CASE("product: the same cell on USB power reports a lower state of charge") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().millivolts = 4000;
    rig.run(0, 12000);
    const uint8_t resting = rig.state().battery.percent;

    rig.platform.battery().external_power = true;
    rig.run(12000, 24000);
    CHECK(rig.state().battery.charging);
    CHECK(rig.state().battery.millivolts == 4000);
    CHECK(rig.state().battery.percent < resting);
}

TEST_CASE("product: a board with no battery sense says so instead of reporting empty") {
    constexpr hal::Capabilities kNoBattery = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Battery));
    Rig rig{kNoBattery};
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.degraded() == hal::Capability::Battery);

    rig.run(0, 12000);
    CHECK_FALSE(rig.state().battery.valid);
    CHECK(rig.state().battery.percent == 0);
}

TEST_CASE("product: settings changed over the link are persisted") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.state().settings.alarm_volume = 5;
    rig.product.config().config().set_flight_state(comms::FlightState::Ground);

    const char* json = "{\"cmd\":\"set\",\"alarm_volume\":2}";
    messages::RxFrame frame{};
    frame.endpoint = messages::Endpoint::Config;
    frame.len = static_cast<uint16_t>(__builtin_strlen(json));
    for (uint16_t i = 0; i < frame.len; i++) frame.data[i] = static_cast<uint8_t>(json[i]);
    rig.platform.link().push_rx(frame);
    rig.run(100, 300);
    rig.product.config().config().confirm();
    rig.run(300, 400);

    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    settings::Settings stored{};
    REQUIRE(settings::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 2);
}

TEST_CASE("product: powering the panel down leaves the wordmark on it") {
    // An e-paper holds its last image with the rails down, so what is written
    // immediately before power_off is what the device wears while it is off.
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 1000);

    ui::Framebuffer expected;
    expected.clear(true);
    ui::draw_wordmark(expected, ui::Framebuffer::kW / 2, ui::Framebuffer::kH / 2);

    rig.product.screen().set_power(false);
    CHECK_FALSE(rig.platform.chips().epd.powered);
    CHECK(rig.platform.chips().epd.last_full);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
    CHECK(expected.count_black() > 200);

    // ... and it stays there: a service that keeps rendering must not reach a
    // panel whose rails are down, or the mark would be wiped a second later.
    rig.run(1000, 4000);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());

    // The mark is above the word: the dot and its arcs put ink in the top half
    // of the glyph block, which the letters alone would leave blank.
    int arc_ink = 0;
    for (int y = 66; y < 78; y++)
        for (int x = 100; x < 160; x++) arc_ink += expected.get_pixel(x, y) ? 1 : 0;
    CHECK(arc_ink > 20);
}

TEST_CASE("product: the backlight starts off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 1000);
    CHECK_FALSE(rig.product.screen().backlight());
    CHECK_FALSE(rig.platform.chips().epd.backlight);
}

// D2: the panel is the self test. A device that returns from main and goes dark
// tells a pilot on a bench nothing at all; a device holding a page that names
// the part that did not answer tells them everything.

TEST_CASE("product: the self-test page reaches the panel before anything refuses") {
    constexpr hal::Capabilities kNoGnss = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Gnss));
    Rig rig{kNoGnss};
    CHECK(rig.setup() == Status::Down);
    CHECK_FALSE(rig.product.flyable());

    // Painted, full, and it is the self-test page rather than a blank glass.
    CHECK(rig.platform.chips().epd.present_count == 1);
    CHECK(rig.platform.chips().epd.last_full);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() ==
          rig.product.boot_page().count_black());
    CHECK(rig.product.boot_page().count_black() > 200);

    // And it stays. The loop refuses to fly, so nothing overwrites the one page
    // that says why.
    rig.run(0, 20000);
    CHECK(rig.platform.chips().epd.present_count == 1);
    CHECK_FALSE(rig.state().started);
}

TEST_CASE("product: the self-test page names the part, not just the failure") {
    constexpr hal::Capabilities kNoGnss = static_cast<hal::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(hal::Capability::Gnss));
    Rig missing{kNoGnss};
    missing.setup();

    Rig whole;
    whole.setup();

    // Row 1 is GNSS (products/skyblip_go/product.h::kBootParts). The two pages
    // differ there and nowhere else in that row's band.
    const ui::Framebuffer& bad = missing.product.boot_page();
    const ui::Framebuffer& good = whole.product.boot_page();
    int row_difference = 0;
    for (int y = ui::boot_row_y(1); y < ui::boot_row_y(1) + 8; y++)
        for (int x = 0; x < ui::Framebuffer::kW; x++)
            row_difference += bad.get_pixel(x, y) != good.get_pixel(x, y) ? 1 : 0;
    CHECK(row_difference > 0);

    // The radio row is identical on both: only the part that failed changed.
    int radio_difference = 0;
    for (int y = ui::boot_row_y(0); y < ui::boot_row_y(0) + 8; y++)
        for (int x = 0; x < ui::Framebuffer::kW; x++)
            radio_difference += bad.get_pixel(x, y) != good.get_pixel(x, y) ? 1 : 0;
    CHECK(radio_difference == 0);
}

TEST_CASE("product: the reset reason is read once at boot and kept") {
    Rig rig;
    rig.platform.system_power().causes = power::ResetCause::Watchdog | power::ResetCause::Software;
    REQUIRE(rig.setup() == Status::Ok);
    // The bite, not the reset that followed it: that is the whole diagnostic
    // value of the register.
    CHECK(rig.product.reset_reason() == power::ResetReason::Watchdog);

    Rig fresh;
    fresh.platform.system_power().causes = power::ResetCause::PowerOn;
    REQUIRE(fresh.setup() == Status::Ok);
    CHECK(fresh.product.reset_reason() == power::ResetReason::PowerOn);
    // Two different boots must not paint the same page.
    CHECK(fresh.product.boot_page().count_black() != rig.product.boot_page().count_black());
}

// D5. The panel has the reason at boot and then it is gone; the field diagnosis
// happens over the link, days later, with the device in a bag.
TEST_CASE("product: the status reply over the link names why the device came up") {
    Rig rig;
    rig.platform.system_power().causes = power::ResetCause::Watchdog;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.link().clear();

    rig.send("{\"cmd\":\"status\"}");
    rig.run(0, 200);
    REQUIRE(rig.platform.link().count_on(messages::Endpoint::Config) == 1);
    CHECK(rig.platform.link().last().bytes.find("WATCHDOG") != std::string::npos);

    // A device that came up because someone pressed the button says that, and
    // not the UNKNOWN a reason nobody passed on would read as.
    Rig pressed;
    pressed.platform.system_power().causes = power::ResetCause::Pin;
    REQUIRE(pressed.setup() == Status::Ok);
    pressed.platform.link().clear();
    pressed.send("{\"cmd\":\"status\"}");
    pressed.run(0, 200);
    CHECK(pressed.platform.link().last().bytes.find("RESET PIN") != std::string::npos);
    CHECK(pressed.platform.link().last().bytes.find("UNKNOWN") == std::string::npos);
}

// B3. The slot map is specified against the PPS edge, and the transmit plan is
// armed from it. An edge rebuilt from the millisecond phase is up to a
// millisecond late on a 5 ms guard.
TEST_CASE("product: the PPS edge on the bus is the edge itself, to the microsecond") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    rig.platform.clock().set_micros(12345678);
    rig.product.step(12345);
    CHECK(rig.state().clock.pps_locked);
    CHECK(rig.state().clock.pps_edge_us == 12000000u);
    CHECK(rig.state().clock.ms_since_pps == 345u);

    // What the same pass used to publish, rebuilt from the phase: the sub-
    // millisecond remainder was thrown away, and it is not a rounding error but
    // a signed lateness on every plan armed from it.
    const uint64_t rebuilt =
        12345678u - static_cast<uint64_t>(rig.state().clock.ms_since_pps) * 1000;
    CHECK(rebuilt - rig.state().clock.pps_edge_us == 678u);

    // No lock, no edge: a stale instant is worse than none, because the phase
    // it implies is a plausible one.
    rig.platform.pps().set_locked(false);
    rig.platform.clock().set_micros(13345678);
    rig.product.step(13345);
    CHECK_FALSE(rig.state().clock.pps_locked);
    CHECK(rig.state().clock.pps_edge_us == 0u);
}

// B4. The one page where a value appears once, so the one page the unit setting
// can decide. What is on the glass changes; the status page's two columns do not.
TEST_CASE("product: the unit setting changes the instrument page a pilot reads") {
    auto sixpack_ink = [](settings::Units units) {
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        rig.state().settings.units = units;
        uint32_t t = 100;
        rig.press(t);  // radar -> six-pack
        rig.run(t, t + 3000);
        REQUIRE(rig.product.screen().page() == go::Page::SixPack);
        return rig.product.screen().framebuffer().count_black();
    };

    CHECK(sixpack_ink(settings::Units::Metric) != sixpack_ink(settings::Units::Imperial));
}

// F5. The device said nothing when the receiver finally solved, and it
// transmitted the first solution it got. Both are wrong on the bench and in the
// air: a cold receiver's first fixes walk, and the pilot is left guessing.
TEST_CASE("product: the first fix is announced once, then own-ship settles before it flies") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 500);
    CHECK_FALSE(rig.product.screen().first_fix().ever_fixed());
    CHECK(int(rig.platform.annunciator().level()) == 0);

    rig.push_fix(1000, 1);
    rig.run(550, 700);
    CHECK(rig.product.screen().first_fix().ever_fixed());
    CHECK(int(rig.platform.annunciator().level()) == 1);  // the chirp, at the lowest step
    // The motor stays out of it: haptics mean traffic that escalated.
    CHECK(rig.platform.annunciator().vibro_ms() == 0);

    // It is short and it stops by itself, so nothing has to remember to silence
    // it before the first traffic contact arrives.
    rig.run(750, 2000);
    CHECK(int(rig.platform.annunciator().level()) == 0);

    // Nothing is worth transmitting yet, and 20 s later it is.
    const gnss::FirstFix& fix = rig.product.screen().first_fix();
    CHECK_FALSE(fix.settled(2000));
    CHECK_FALSE(fix.settled(fix.fix_since_ms() + gnss::kFirstFixSettleMs - 1));
    CHECK(fix.settled(fix.fix_since_ms() + gnss::kFirstFixSettleMs));

    // A second acquisition is not a first one: no second chirp, and the shorter
    // wait applies.
    gnss::GnssFix lost{};
    lost.valid = false;
    rig.product.bus().gnss.push(lost);
    rig.run(2050, 2200);
    CHECK_FALSE(fix.settled(2200));
    rig.push_fix(1000, 2);
    rig.run(2250, 2400);
    CHECK(int(rig.platform.annunciator().level()) == 0);
    CHECK(fix.settled(fix.fix_since_ms() + gnss::kRefixSettleMs));
}

// B4 + the low-battery warning: two things the status page is the only reader
// of. Both are drawn from the state the services publish, so this is the wiring
// test - ui/screens/status.cpp owns what they look like.
TEST_CASE("product: the status page carries the device's name and a cell that is low") {
    auto status_ink = [](const char* callsign, uint16_t millivolts, bool on_cable = false) {
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        rig.platform.battery().millivolts = millivolts;
        rig.platform.battery().external_power = on_cable;
        for (int i = 0; callsign[i] != 0 && i < 9; i++)
            rig.state().settings.callsign[i] = callsign[i];
        uint32_t t = 100;
        rig.press(t);  // radar -> six-pack
        rig.press(t);  // -> status
        // Long enough for the cutoff monitor to have made its mind up: it wants
        // three consecutive samples before it calls a cell low, and the page
        // draws what it decided rather than deciding again.
        rig.run(t, t + 8000);
        REQUIRE(rig.product.screen().page() == go::Page::Status);
        return rig.product.screen().framebuffer().count_black();
    };

    const int plain = status_ink("", 4050);
    CHECK(status_ink("D-KXYZ", 4050) > plain);
    // 3.45 V is under core/power's warning threshold: the row says LOW rather
    // than leaving a pilot to read the number and know what it means. The same
    // cell on the cable is charging, and nothing on a charger is low.
    const int low = status_ink("", 3450);
    CHECK(low != plain);
    CHECK(status_ink("", 3450, /*on_cable=*/true) != low);
}

// D3, the reporting half. The marker on the page is the cutoff monitor's own
// verdict, published on the bus: the page does not compare millivolts a second
// time, so it cannot disagree with the thing that can switch the device off.
TEST_CASE("product: the status page marks a low cell when the monitor says so, not before") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().millivolts = 3450;
    uint32_t t = 100;
    rig.press(t);  // radar -> six-pack
    rig.press(t);  // -> status
    REQUIRE(rig.product.screen().page() == go::Page::Status);

    // Two samples under the warning is not yet a low cell: a transmit burst
    // sags the rail for as long as it lasts, and the third sample is what
    // decides. The page says nothing while the monitor has not.
    rig.run(t, 2500);
    REQUIRE(rig.state().power_level != power::PowerLevel::Low);
    const int undecided = rig.product.screen().framebuffer().count_black();

    rig.run(2500, 6000);
    REQUIRE(rig.state().power_level == power::PowerLevel::Low);
    // The same voltage and the same state of charge, so the only thing that can
    // have changed on the glass is the marker.
    CHECK(rig.product.screen().framebuffer().count_black() > undecided);
}
