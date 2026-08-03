// The housekeeping a device needs to survive being switched on, wired up to the
// real product on the host platform: the way out (long press, cutoff, link), the
// gauge that acts rather than reports, the loop's half of the watchdog, and the
// two wires that decide whether the companion link can do anything at all - the
// flight state behind the gate, and the gesture that answers a prompt.
// Nothing here is mocked below the services - the same board, the same service
// list, the same part models the silicon build uses.
#include <cstring>

#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"
#include "runtime/tasks.h"
#include "ui/screens/confirm.h"

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

    // The authorising gesture: two presses well inside ui::ConfirmGesture's
    // window. Each press() above advances the clock by ~200 ms, so the pair
    // lands at a gap a thumb actually produces.
    void double_press(uint32_t& t) {
        press(t);
        press(t);
    }

    // One solution from the receiver. core/flight decides what it means and
    // publishes the ADS-L code; nothing here tells the companion link anything.
    void push_fix(uint16_t speed_q, int32_t alt_msl_m) {
        gnss::GnssFix fix{};
        fix.valid = true;
        fix.speed_q = speed_q;
        fix.alt_msl_m = alt_msl_m;
        fix.updates = 1;
        product.bus().gnss.push(fix);
    }

    void on_ground(uint32_t& t) {
        push_fix(/*speed_q=*/0, /*alt_msl_m=*/0);
        run(t, t + 200);
        t += 200;
    }

    // core/flight wants five seconds of motion it believes before it will call
    // a device airborne, and it throws the first sample away as too jerky, so
    // this is a climb-out rather than one hopeful fix.
    void airborne(uint32_t& t) {
        for (int i = 0; i < 14; i++) {
            push_fix(/*speed_q=*/200, /*alt_msl_m=*/1200);
            run(t, t + 500);
            t += 500;
        }
    }

    bus::State& state() { return product.state(); }

    // A companion app talking to the device: the frame arrives on the link the
    // board polls, so it takes the same road a phone's would.
    void send(const char* json) {
        messages::RxFrame frame{};
        frame.endpoint = messages::Endpoint::Config;
        frame.len = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.len);
        platform.link().push_rx(frame);
    }

    comms::ConfigService& config() { return product.config().config(); }
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

// D4 over the link: the same road, from a phone instead of a thumb.

TEST_CASE("product: a confirmed power_off over the link parks the device like a long press") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);
    rig.run(t, 1000);

    rig.send("{\"cmd\":\"power_off\"}");
    rig.run(1000, 1500);
    // Asking is not doing: the request is still waiting for the confirmation
    // the device demands, and nothing has moved.
    CHECK(rig.config().pending() == comms::Pending::PowerOff);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.product.board().rf().sleeps() == 0);

    rig.config().confirm();
    rig.run(1500, 2000);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::LinkRequest);
    CHECK(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    CHECK(rig.product.board().rf().sleeps() == 1);
    CHECK_FALSE(rig.product.screen().powered());

    // The latch is consumed, not left standing: a device that came back up with
    // it set would power itself off again.
    CHECK_FALSE(rig.config().power_off_requested());

    // No button was ever down, so there is no release to wait for.
    rig.run(2000, 2000 + power::kParkMs + power::kReleaseSettleMs + 500);
    CHECK(rig.product.ready_to_power_off());
}

TEST_CASE("product: an unconfirmed power_off over the link turns nothing off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);
    rig.send("{\"cmd\":\"power_off\"}");
    rig.run(t, 5000);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.product.screen().powered());
    CHECK(rig.product.board().rf().sleeps() == 0);

    // And one that was cancelled stays cancelled.
    rig.config().cancel();
    rig.run(5000, 8000);
    CHECK_FALSE(rig.product.shutdown().going_down());

    // Airborne, the device refuses to arm it at all: a link request cannot land
    // an aircraft.
    Rig flying;
    REQUIRE(flying.setup() == Status::Ok);
    uint32_t ft = 0;
    flying.airborne(ft);
    flying.send("{\"cmd\":\"power_off\"}");
    flying.run(ft, ft + 2000);
    ft += 2000;
    CHECK(flying.config().pending() == comms::Pending::None);
    flying.config().confirm();
    flying.run(ft, ft + 2000);
    CHECK_FALSE(flying.product.shutdown().going_down());
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
    // The level is published on the bus, which is where the status page reads
    // it: nothing downstream compares millivolts a second time.
    CHECK(warned.state().power_level == power::PowerLevel::Low);
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

// The two dead wires, end to end on the real product. Neither had a caller: the
// gate was never told the flight state, so it refused everything forever, and
// nothing could ever call confirm(), so it refused everything twice over.

TEST_CASE("product: the gate opens on the ground the fix stream proved, not on a phone's word") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;

    // No solution yet: the device does not know where it is, so it is not on
    // the ground, so it authorises nothing. This is the state a bench device
    // was stuck in - Unknown, forever.
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, 500);
    CHECK(rig.config().flight_state() == comms::FlightState::Unknown);
    CHECK(rig.config().pending() == comms::Pending::None);
    t = 500;

    rig.on_ground(t);
    CHECK(rig.config().flight_state() == comms::FlightState::Ground);
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 500);
    t += 500;
    CHECK(rig.config().pending() == comms::Pending::Dfu);

    // And the same stream takes it away again: the transmitter and the update
    // lockout are reading one decision, not two.
    rig.airborne(t);
    CHECK(rig.config().flight_state() == comms::FlightState::Airborne);
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
}

// The case the whole gesture was chosen for.
TEST_CASE("product: a page press does not authorise a firmware upload") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);

    // A pilot pages through the screens. Nothing is pending, so this is paging
    // and only paging.
    rig.press(t);
    rig.run(t, t + 200);
    t += 200;
    REQUIRE(rig.product.screen().page() == go::Page::SixPack);

    // Long enough for the question to have reached the glass: a press only
    // counts towards an answer once the prompt is on the panel and the thumb
    // has stopped, so a page press made before either is simply dropped.
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 3000);
    t += 3000;
    REQUIRE(rig.config().pending() == comms::Pending::Dfu);

    // ... and pages again, exactly as before, on a device that is now asking
    // for permission to be overwritten.
    rig.press(t);
    rig.run(t, t + ui::ConfirmGesture::kDoublePressMs + 200);
    t += ui::ConfirmGesture::kDoublePressMs + 200;

    CHECK_FALSE(rig.config().upload_allowed());
    CHECK(rig.config().pending() == comms::Pending::None);
    // The press was spent refusing, not paging: one press cannot mean two
    // things, and the page it would have turned to is still not showing.
    CHECK(rig.product.screen().page() == go::Page::SixPack);

    // With the prompt gone, the same press pages again.
    rig.press(t);
    rig.run(t, t + 200);
    CHECK(rig.product.screen().page() == go::Page::Status);
}

TEST_CASE("product: two presses on the ground are what open the upload window") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);

    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 3000);
    t += 3000;
    REQUIRE(rig.config().pending() == comms::Pending::Dfu);
    REQUIRE_FALSE(rig.config().upload_allowed());

    rig.double_press(t);
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.config().upload_allowed());
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
    // The page it was on is the page it comes back to.
    CHECK(rig.product.screen().page() == go::Page::Radar);

    // The same two presses, airborne. Fail closed: the window shuts on takeoff
    // and no gesture reopens it.
    rig.airborne(t);
    CHECK_FALSE(rig.config().upload_allowed());
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 3000);
    t += 3000;
    rig.double_press(t);
    rig.run(t, t + 200);
    CHECK_FALSE(rig.config().upload_allowed());
}

TEST_CASE("product: the panel names the operation while it waits, and stops cycling pages") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);

    rig.send("{\"cmd\":\"power_off\"}");
    rig.run(t, t + 6000);
    t += 6000;
    REQUIRE(rig.product.screen().prompt() == comms::Pending::PowerOff);

    ui::ConfirmSnapshot expect;
    expect.title = comms::pending_title(comms::Pending::PowerOff);
    expect.detail = comms::pending_detail(comms::Pending::PowerOff);
    expect.timeout_s = comms::kConfirmWindowMs / 1000;
    ui::Framebuffer expected;
    ui::draw_confirm(expected, expect);
    CHECK(std::memcmp(rig.product.screen().framebuffer().data(), expected.data(),
                      ui::Framebuffer::kBytes) == 0);

    // It reached the glass, not just the buffer: the pilot being asked can see
    // the question before the button can answer it.
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
}

TEST_CASE("product: a prompt nobody answers expires, and the device is not powered off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.on_ground(t);

    rig.send("{\"cmd\":\"power_off\"}");
    rig.run(t, t + 1000);
    t += 1000;
    REQUIRE(rig.product.screen().prompt() == comms::Pending::PowerOff);

    rig.run(t, t + comms::kConfirmWindowMs + 1000);
    t += comms::kConfirmWindowMs + 1000;
    CHECK(rig.config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
    CHECK_FALSE(rig.product.shutdown().going_down());

    // And the panel is back on the page the pilot left it on.
    CHECK(rig.product.screen().page() == go::Page::Radar);
    rig.press(t);
    rig.run(t, t + 200);
    CHECK(rig.product.screen().page() == go::Page::SixPack);
}
