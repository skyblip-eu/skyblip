// The acceptance invariant on the host: the real product — board, services,
// drivers — links and runs on the host platform with zero framework code. If any
// of it leaks a Zephyr include, this stops compiling.
#include "core/flight/atmosphere.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"
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
    // The boot frame, presented full; the sky then stays static, so nothing
    // else reaches the glass.
    CHECK(rig.platform.chips().epd.present_count == 1);
    CHECK(rig.platform.chips().epd.last_full);

    rig.push_fix(1000, 1);  // fix arrives: the radar page changes
    rig.run(5000, 8000);
    CHECK(rig.platform.chips().epd.present_count == 2);
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
    settings::Settings s = settings::defaults(0x111111);
    s.alarm_volume = 1;
    uint8_t blob[64];
    settings::to_blob(s, blob, sizeof(blob));
    REQUIRE(rig.platform.kv().write("settings", blob, settings::blob_size()) == Status::Ok);

    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.state().settings.alarm_volume == 1);
    CHECK(rig.state().settings.device_addr == 0x111111);
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
