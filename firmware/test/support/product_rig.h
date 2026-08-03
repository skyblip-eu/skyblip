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

}  // namespace skyblip

#endif
