// The housekeeping a device needs to survive being switched on, wired up to the
// real product on the host platform: the way out (long press, cutoff, link), the
// gauge that acts rather than reports, and the loop's half of the watchdog.
// Nothing here is mocked below the services - the same board, the same service
// list, the same part models the silicon build uses.
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"
#include "runtime/tasks.h"

using namespace skyblip;

namespace {

using Go = go::Product<platform::host::Platform>;

struct Rig {
    platform::host::Platform platform;
    Go product{platform};

    Status setup() { return product.setup(); }

    void run(uint32_t from, uint32_t to, uint32_t step = 50) {
        for (uint32_t t = from; t <= to; t += step) {
            platform.clock().set_millis(t);
            product.step(t);
        }
    }

    // The raw level, held for as long as a thumb would. A long press produces no
    // edges at all, so nothing on the ButtonEvent path can see one.
    void hold_button(uint32_t& t, uint32_t ms, bool down = true) {
        platform.board_gpio().button_down = down;
        const uint32_t until = t + ms;
        for (; t <= until; t += 50) {
            platform.clock().set_millis(t);
            product.step(t);
        }
    }

    // Held across steps, then released: ui::Button only reports a press once a
    // level has been stable through its debounce window.
    void press(uint32_t& t) {
        hold_button(t, 80, /*down=*/true);
        hold_button(t, 80, /*down=*/false);
    }

    bus::State& state() { return product.state(); }
};

}  // namespace

// D4: the way out. The radio is asked to sleep, the panel is parked, and only
// then are the rails allowed to drop - and not while the button is still down.

TEST_CASE("product: a long press parks the radio and the panel, then asks for the rails") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(0, 1000);
    t = 1000;
    REQUIRE(rig.product.board().rf().sleeps() == 0);

    rig.hold_button(t, power::kLongPressMs + 200);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LongPress);
    CHECK(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    // A receiver armed through most of every second is what flattens the pack,
    // so it is the first thing told to stop.
    CHECK(rig.product.board().rf().sleeps() == 1);
    CHECK_FALSE(rig.product.screen().powered());
    CHECK_FALSE(rig.platform.chips().epd.powered);

    // Still held: the rails must not go, or a level-sensed wake pin brings the
    // device straight back up.
    rig.hold_button(t, power::kParkMs + 1000);
    CHECK_FALSE(rig.product.ready_to_power_off());

    rig.hold_button(t, power::kReleaseSettleMs + 200, /*down=*/false);
    CHECK(rig.product.ready_to_power_off());
}

TEST_CASE("product: a page press is not a power-off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.press(t);
    rig.run(t, t + 2000);
    CHECK(rig.product.screen().page() == go::Page::SixPack);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.product.board().rf().sleeps() == 0);
}

// D3: the acting half of the gauge, wired up.

TEST_CASE("product: a cell at its cutoff takes the device down on its own") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 2000);
    REQUIRE_FALSE(rig.product.power().cutoff());

    rig.platform.battery().millivolts = 3100;
    rig.run(2000, 8000);
    CHECK(rig.product.power().cutoff());
    CHECK(rig.product.shutdown().going_down());
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);
    CHECK(rig.product.board().rf().sleeps() == 1);

    rig.run(8000, 20000);
    CHECK(rig.product.ready_to_power_off());
}

TEST_CASE("product: a warning comes before the cutoff, and a floating sense never acts") {
    Rig warned;
    REQUIRE(warned.setup() == Status::Ok);
    warned.platform.battery().millivolts = 3400;
    warned.run(0, 8000);
    CHECK(warned.product.power().warning());
    CHECK_FALSE(warned.product.power().cutoff());
    CHECK_FALSE(warned.product.shutdown().going_down());

    // An unconnected divider drifts near zero. It must not switch a device off
    // in someone's hand.
    Rig floating;
    REQUIRE(floating.setup() == Status::Ok);
    floating.platform.battery().millivolts = 200;
    floating.run(0, 20000);
    CHECK(floating.product.power().implausible_samples() > 3);
    CHECK_FALSE(floating.product.power().cutoff());
    CHECK_FALSE(floating.product.shutdown().going_down());
}

// D1: the loop's half of the watchdog, at product scale.

TEST_CASE("product: the loop feeds the watchdog while it is flying and through a shutdown") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    for (uint32_t t = 0; t <= 3 * runtime::kTaskWatchdogMs; t += 100) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        CHECK(rig.product.may_feed_watchdog(t));
    }

    // A device that is deliberately going down is doing what it was told: a
    // held button must not turn a power-off into a watchdog reboot.
    uint32_t t = 3 * runtime::kTaskWatchdogMs;
    rig.hold_button(t, power::kLongPressMs + 200);
    REQUIRE(rig.product.shutdown().going_down());
    rig.hold_button(t, 2 * runtime::kTaskWatchdogMs);
    CHECK(rig.product.may_feed_watchdog(t));
}
