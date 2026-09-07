// The screen service alone over the real SSD1681 driver and its model, state written by hand.
#ifndef SKYBLIP_TEST_SUPPORT_SCREEN_RIG_H
#define SKYBLIP_TEST_SUPPORT_SCREEN_RIG_H

#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "hardware/platform/host/clock.h"
#include "products/skyblip_go/services/screen.h"
#include "runtime/null.h"

using namespace skyblip;

namespace {

// The screen service alone: state is written by hand, so alarm level and
// traffic are exactly what the case says, with no alarm service re-deriving
// them.
struct Rig {
    models::Ssd1681 chip;
    parts::Ssd1681 epd{chip, chip, chip.dc, chip.rst, chip.busy};
    platform::host::Clock clock;
    runtime::NullRoles null;
    hal::Roles roles{clock,   null.rf,        null.link,        epd,  // epd fills Display
                     null.kv, null.log_flash, null.annunciator, null.dfu};
    bus::Bus bus{};
    bus::State state{};
    runtime::Context context{roles, bus, state};
    go::ScreenService screen{context};

    Rig() {
        roles.capabilities = hal::Capability::Display;
        // With a fix the radar page draws rings and the range label, so churn()
        // below produces real pixel changes.
        state.own.fix_valid = true;
        epd.begin();
    }

    // One service tick per second, the render cadence.
    void run_seconds(uint32_t& t, int seconds) {
        for (int i = 0; i < seconds; i++) {
            t += 1000;
            screen.tick(t);
        }
    }

    // Forces a visible change every second: the coverage indicator character
    // on the radar page flips. Orthogonal to alarm level and traffic count, so
    // the quiet-sky logic stays in the case's hands.
    void churn(uint32_t& t, int seconds) {
        for (int i = 0; i < seconds; i++) {
            state.clock.utc_valid = !state.clock.utc_valid;
            run_seconds(t, 1);
        }
    }

    void alarm(uint8_t level) { state.alarm_level = level; }

    void die_temperature(int16_t decicelsius) {
        state.die_decicelsius = decicelsius;
        state.die_temperature_valid = true;
    }
};

}  // namespace

#endif
