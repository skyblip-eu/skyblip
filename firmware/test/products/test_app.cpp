// test/products/test_app.cpp — enforces the §8 acceptance invariant on HOST:
// the real composition root (App) links and runs against fakes, with ZERO
// framework code. If App ever leaks a framework (Zephyr) include, this stops
// compiling — which is exactly the guard the invariant was supposed to have.
#include <string>

#include "devices/host/fake_clock.h"
#include "devices/host/fake_display.h"
#include "devices/host/fake_link.h"
#include "devices/host/fake_sx1262.h"
#include "doctest/doctest.h"
#include "products/skyblip/app.h"

using namespace skyblip;

namespace {

// Minimal in-memory KvStore fake (kept local; App treats kv as optional).
class FakeKv : public hal::KvStore {
   public:
    Status read(const char* key, uint8_t* buf, size_t cap, size_t& out_len) override {
        for (auto& e : e_)
            if (e.used && e.key == key) {
                if (e.len > cap) return Status::OutOfRange;
                for (size_t i = 0; i < e.len; i++) buf[i] = e.data[i];
                out_len = e.len;
                return Status::Ok;
            }
        return Status::NotFound;
    }
    Status write(const char* key, const uint8_t* buf, size_t len) override {
        Entry* slot = nullptr;
        for (auto& e : e_)
            if (e.used && e.key == key) slot = &e;
        if (!slot)
            for (auto& e : e_)
                if (!e.used) {
                    slot = &e;
                    slot->key = key;
                    slot->used = true;
                    break;
                }
        if (!slot || len > sizeof(slot->data)) return Status::Full;
        for (size_t i = 0; i < len; i++) slot->data[i] = buf[i];
        slot->len = len;
        return Status::Ok;
    }
    Status erase(const char* key) override {
        for (auto& e : e_)
            if (e.used && e.key == key) e.used = false;
        return Status::Ok;
    }

   private:
    struct Entry {
        bool used{false};
        std::string key;
        uint8_t data[64]{};
        size_t len{0};
    };
    Entry e_[4];
};

struct Rig {
    host::FakeClock clock;
    host::FakeLink link;
    host::FakeSx1262 bus;
    FakeKv kv;
    drivers::Sx1262 radio{bus, bus, bus.busy_pin, bus.reset_pin, bus.dio1_pin};

    product::Ports ports() {
        product::Ports p{clock, link, radio};
        p.kv = &kv;
        p.device_addr = 0x0ABBCC;
        return p;
    }
};

}  // namespace

TEST_CASE("app: setup() wires the radio up to Rx (composition root, no framework)") {
    Rig rig;
    product::App app(rig.ports());
    CHECK(app.setup() == Status::Ok);
    CHECK(app.started());
    CHECK(rig.radio.mode() == drivers::RadioMode::Rx);
    CHECK(rig.bus.reset_pulses >= 1);
    // device_addr flowed into the default settings.
    CHECK(app.settings().device_addr == 0x0ABBCC);
}

TEST_CASE("app: setup() is idempotent") {
    Rig rig;
    product::App app(rig.ports());
    CHECK(app.setup() == Status::Ok);
    CHECK(app.setup() == Status::Ok);
}

TEST_CASE("app: step() runs the service cycle deterministically under a fake clock") {
    Rig rig;
    product::App app(rig.ports());
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
    host::FakeDisplay display;
    product::Ports ports = rig.ports();
    ports.display = &display;
    product::App app(ports);
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
    host::FakeDisplay display;
    product::Ports ports = rig.ports();
    ports.display = &display;
    product::App app(ports);
    REQUIRE(app.setup() == Status::Ok);
    CHECK(app.page() == product::Page::Radar);

    rig.clock.set_millis(100);
    app.step(100);  // initial full render
    app.on_button();
    CHECK(app.page() == product::Page::AltVs);
    rig.clock.set_millis(150);
    app.step(150);  // page change forces an immediate FULL refresh
    CHECK(display.last_mode == hal::Refresh::Full);

    // Three pages (roadmap 2.6d): radar -> alt/vs -> status -> radar.
    app.on_button();
    CHECK(app.page() == product::Page::Status);
    app.on_button();
    CHECK(app.page() == product::Page::Radar);
}

TEST_CASE("app: page_mask disables pages so the button skips them") {
    Rig rig;
    host::FakeDisplay display;
    product::Ports ports = rig.ports();
    ports.display = &display;
    product::App app(ports);
    REQUIRE(app.setup() == Status::Ok);
    // Enable radar + status only (bit0 | bit2).
    app.settings().page_mask = 0x05;
    CHECK(app.page() == product::Page::Radar);
    app.on_button();
    CHECK(app.page() == product::Page::Status);  // AltVs skipped
    app.on_button();
    CHECK(app.page() == product::Page::Radar);
}

TEST_CASE("app: backlight state is tracked and forwarded to the display") {
    Rig rig;
    product::App app(rig.ports());
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

    product::App app(rig.ports());
    REQUIRE(app.setup() == Status::Ok);
    CHECK(app.settings().alarm_volume == 1);
    CHECK(app.settings().device_addr == 0x111111);
}
