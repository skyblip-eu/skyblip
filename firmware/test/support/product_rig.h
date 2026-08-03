// Harness, not a test: the whole product on the host platform, stepped under a
// clock the case advances. Every product suite drives the device through this,
// so a case reads as what a pilot or a companion app does to the unit, and
// nothing below the services is stubbed out.
#ifndef SKYBLIP_TEST_SUPPORT_PRODUCT_RIG_H
#define SKYBLIP_TEST_SUPPORT_PRODUCT_RIG_H

#include <cstring>

#include "core/flight/atmosphere.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"

namespace skyblip {

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

    // 2026-08-02T00:00:00Z. A fix with no UTC cannot name a session, so the
    // helpers below carry one and advance it a second at a time.
    static constexpr uint32_t kUtcBase = 1785628800;

    // A solution as a receiver reports one in flight: moving, timed, and
    // referenced to both datums. core/flight decides what it means.
    void push_timed_fix(uint16_t speed_q, int32_t alt_msl_m) {
        gnss::GnssFix f{};
        f.valid = true;
        f.utc_valid = true;
        f.utc = kUtcBase + utc_offset_s;
        f.lat_1e7 = 485000000 + static_cast<int32_t>(utc_offset_s) * 3000;
        f.lon_1e7 = 85000000;
        f.alt_msl_m = alt_msl_m;
        f.alt_m = alt_msl_m + gnss::kDefaultGeoidSeparationM;
        f.geoid_separation_measured = true;
        f.speed_q = speed_q;
        f.track_c9 = 128;
        f.sats = 10;
        f.hdop_e2 = 100;
        f.updates = ++fix_updates;
        product.bus().gnss.push(f);
    }

    // One second of the world: a solution, then the passes that follow it.
    void second(uint32_t& t, uint16_t speed_q, int32_t alt_msl_m) {
        push_timed_fix(speed_q, alt_msl_m);
        run(t, t + 950);
        t += 1000;
        utc_offset_s++;
    }

    void seconds(uint32_t& t, uint32_t n, uint16_t speed_q, int32_t alt_msl_m) {
        for (uint32_t i = 0; i < n; i++) second(t, speed_q, alt_msl_m);
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

    // The authorising gesture: two presses inside ui::ConfirmGesture's window.
    void double_press(uint32_t& t) {
        press(t);
        press(t);
    }

    bus::State& state() { return product.state(); }

    // A central connects, and a central goes away, through the platform's own
    // link model. Nothing here reaches into a service: the event travels
    // platform -> board -> bus -> config service, which is the path silicon uses.
    void raise_link(uint16_t session_id = 1) { platform.link().raise_link(session_id); }
    void drop_link() { platform.link().drop_link(); }
    bool link_up() { return product.config().config().link_up(); }

    // The companion app's side of the link, arriving where the board polls it.
    void send(const char* json) {
        messages::RxFrame frame{};
        frame.endpoint = messages::Endpoint::Config;
        frame.len = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.len);
        platform.link().push_rx(frame);
    }

    // The same app on the log endpoint, which is a separate characteristic and
    // a separate queue.
    void send_log(const char* json) {
        messages::RxFrame frame{};
        frame.endpoint = messages::Endpoint::Log;
        frame.len = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.len);
        platform.link().push_rx(frame);
    }

    uint32_t utc_offset_s{0};
    uint32_t fix_updates{0};
};

// A board with no fitted barometer: the samples in these cases are pushed by
// hand, so the board must not also be pumping its own.
constexpr hal::Capabilities kBaroByHand =
    static_cast<hal::Capabilities>(static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
                                   ~static_cast<uint32_t>(hal::Capability::Baro));

}  // namespace skyblip

#endif
