// test/products/test_app.cpp — enforces the §8 acceptance invariant on HOST:
// the real composition root (App) links and runs against device models, with ZERO
// framework code. If App ever leaks a framework (Zephyr) include, this stops
// compiling — which is exactly the guard the invariant was supposed to have.
#include "core/flight/atmosphere.h"
#include "devices/models/annunciator.h"
#include "devices/models/clock.h"
#include "devices/models/display.h"
#include "devices/models/kvstore.h"
#include "devices/models/link.h"
#include "devices/models/sx1262.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/app.h"

using namespace skyblip;

namespace {

struct Rig {
    models::Clock clock;
    models::Link link;
    models::Sx1262 bus;
    models::KvStore kv;
    drivers::Sx1262 radio{bus, bus, bus.busy_pin, bus.reset_pin, bus.dio1_pin};

    go::Ports ports() {
        go::Ports p{clock, link, radio};
        p.kv = &kv;
        p.device_addr = 0x0ABBCC;
        return p;
    }
};

}  // namespace

TEST_CASE("app: setup() wires the radio up to Rx (composition root, no framework)") {
    Rig rig;
    go::App app(rig.ports());
    CHECK(app.setup() == Status::Ok);
    CHECK(app.started());
    CHECK(rig.radio.mode() == drivers::RadioMode::Rx);
    CHECK(rig.bus.reset_pulses >= 1);
    // device_addr flowed into the default settings.
    CHECK(app.settings().device_addr == 0x0ABBCC);
}

TEST_CASE("app: setup() is idempotent") {
    Rig rig;
    go::App app(rig.ports());
    CHECK(app.setup() == Status::Ok);
    CHECK(app.setup() == Status::Ok);
}

TEST_CASE("app: step() runs the service cycle deterministically under a modelled clock") {
    Rig rig;
    go::App app(rig.ports());
    REQUIRE(app.setup() == Status::Ok);
    for (uint32_t t = 0; t <= 3000; t += 100) {
        rig.clock.set_millis(t);
        app.step(t);
    }
    // A queued good frame is delivered through poll without faulting the driver.
    uint8_t pkt[6] = {1, 2, 3, 4, 5, 6};
    rig.bus.queue_rx(pkt, sizeof(pkt));
    rig.clock.advance(100);
    app.step(rig.clock.millis());
    CHECK(rig.radio.reinit_count() == 0);  // never went silent long enough to reinit
}

TEST_CASE("app: the e-paper is refreshed on cadence when a display is present") {
    Rig rig;
    models::Display display;
    go::Ports ports = rig.ports();
    ports.display = &display;
    go::App app(ports);
    REQUIRE(app.setup() == Status::Ok);
    for (uint32_t t = 0; t <= 3000; t += 100) {
        rig.clock.set_millis(t);
        app.step(t);
    }
    // ~1 Hz render cadence over 3 s → at least 3 presents.
    CHECK(display.present_count >= 3);
    CHECK(display.last_mode == hal::Refresh::Partial);
}

TEST_CASE("app: a button press switches the page and forces a full refresh") {
    Rig rig;
    models::Display display;
    go::Ports ports = rig.ports();
    ports.display = &display;
    go::App app(ports);
    REQUIRE(app.setup() == Status::Ok);
    CHECK(app.page() == go::Page::Radar);

    rig.clock.set_millis(100);
    app.step(100);  // initial full render
    app.on_button();
    CHECK(app.page() == go::Page::AltVs);
    rig.clock.set_millis(150);
    app.step(150);  // page change forces an immediate FULL refresh
    CHECK(display.last_mode == hal::Refresh::Full);

    // Three pages (roadmap 2.6d): radar -> alt/vs -> status -> radar.
    app.on_button();
    CHECK(app.page() == go::Page::Status);
    app.on_button();
    CHECK(app.page() == go::Page::Radar);
}

TEST_CASE("app: page_mask disables pages so the button skips them") {
    Rig rig;
    models::Display display;
    go::Ports ports = rig.ports();
    ports.display = &display;
    go::App app(ports);
    REQUIRE(app.setup() == Status::Ok);
    // Enable radar + status only (bit0 | bit2).
    app.settings().page_mask = 0x05;
    CHECK(app.page() == go::Page::Radar);
    app.on_button();
    CHECK(app.page() == go::Page::Status);  // AltVs skipped
    app.on_button();
    CHECK(app.page() == go::Page::Radar);
}

TEST_CASE("app: backlight state is tracked and forwarded to the display") {
    Rig rig;
    go::App app(rig.ports());
    REQUIRE(app.setup() == Status::Ok);
    CHECK_FALSE(app.backlight());
    app.set_backlight(true);
    CHECK(app.backlight());
}

TEST_CASE("app: persisted settings are loaded on setup()") {
    Rig rig;
    settings::Settings s = settings::defaults(0x111111);
    s.alarm_volume = 1;
    uint8_t blob[64];
    settings::to_blob(s, blob, sizeof(blob));
    REQUIRE(rig.kv.write("settings", blob, settings::blob_size()) == Status::Ok);

    go::App app(rig.ports());
    REQUIRE(app.setup() == Status::Ok);
    CHECK(app.settings().alarm_volume == 1);
    CHECK(app.settings().device_addr == 0x111111);
}

TEST_CASE("app: barometric pressure drives vertical speed") {
    Rig rig;
    go::App app(rig.ports());
    REQUIRE(app.setup() == Status::Ok);
    CHECK_FALSE(app.baro_active());

    // Climb 1000 m -> 1010 m over 2 s, expressed only as pressure.
    app.on_baro(flight::alt_cm_to_pressure(100000), 1000);
    CHECK(app.baro_active());
    app.on_baro(flight::alt_cm_to_pressure(101000), 3000);

    // +10 m in 2 s = +5 m/s = 40 eighth-m/s.
    CHECK(app.own().climb_e8 == doctest::Approx(40).epsilon(0.05));
}

TEST_CASE("app: a sample inside the window is ignored, not extrapolated") {
    Rig rig;
    go::App app(rig.ports());
    REQUIRE(app.setup() == Status::Ok);
    app.on_baro(flight::alt_cm_to_pressure(100000), 1000);
    app.on_baro(flight::alt_cm_to_pressure(199000), 1100);  // 100 ms later
    CHECK(app.own().climb_e8 == 0);                         // no absurd rate published
}

TEST_CASE("app: with no barometer, vertical speed still comes from GNSS") {
    Rig rig;
    go::App app(rig.ports());
    REQUIRE(app.setup() == Status::Ok);

    gnss::GnssFix f{};
    f.valid = true;
    f.alt_m = 1000;
    f.updates = 1;
    app.on_gnss_fix(f);
    app.step(1000);

    f.alt_m = 1010;  // +10 m over the 2 s GNSS window
    f.updates = 2;
    app.on_gnss_fix(f);
    app.step(3000);

    CHECK_FALSE(app.baro_active());
    CHECK(app.own().climb_e8 == doctest::Approx(40).epsilon(0.05));
}

TEST_CASE("app: once the barometer speaks, GNSS stops setting vertical speed") {
    Rig rig;
    go::App app(rig.ports());
    REQUIRE(app.setup() == Status::Ok);

    app.on_baro(flight::alt_cm_to_pressure(100000), 500);
    app.on_baro(flight::alt_cm_to_pressure(100200), 1500);  // +2 m in 1 s = +16 e8
    const int16_t from_baro = app.own().climb_e8;
    CHECK(from_baro == doctest::Approx(16).epsilon(0.1));

    // A GNSS altitude sequence implying a wildly different rate must not win.
    gnss::GnssFix f{};
    f.valid = true;
    f.alt_m = 1000;
    f.updates = 1;
    app.on_gnss_fix(f);
    app.step(2000);
    f.alt_m = 1200;
    f.updates = 2;
    app.on_gnss_fix(f);
    app.step(4000);

    CHECK(app.own().climb_e8 == from_baro);
}
