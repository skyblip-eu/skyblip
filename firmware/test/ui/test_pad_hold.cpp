// The gesture that opens the settings mode: the pad held on its own.
#include "doctest/doctest.h"
#include "ui/input/pad_hold.h"

using namespace skyblip;

namespace {

constexpr uint32_t kHold = ui::PadHold::kHoldMs;

bool step(ui::PadHold& pad, bool pad_down, bool button_down, uint32_t& t, uint32_t ms) {
    bool fired = false;
    for (uint32_t elapsed = 0; elapsed <= ms; elapsed += 10) {
        fired = pad.update(pad_down, button_down, t + elapsed) || fired;
    }
    t += ms;
    return fired;
}

}  // namespace

TEST_CASE("pad hold: the pad held alone opens the way in, once") {
    ui::PadHold pad;
    uint32_t t = 1000;
    CHECK_FALSE(step(pad, true, false, t, kHold - 100));
    CHECK(step(pad, true, false, t, 200));

    // Held on past the threshold it does not fire again: one gesture, one entry.
    CHECK_FALSE(step(pad, true, false, t, 5000));
}

TEST_CASE("pad hold: a touch shorter than the hold means nothing") {
    ui::PadHold pad;
    uint32_t t = 1000;
    CHECK_FALSE(step(pad, true, false, t, kHold - 200));
    CHECK_FALSE(step(pad, false, false, t, 100));
    CHECK_FALSE(step(pad, true, false, t, kHold - 200));
}

// A hold with the button down is the stow (core/power/shutdown.h), never an entry.
TEST_CASE("pad hold: a hold the button joins is never an entry") {
    ui::PadHold pad;
    uint32_t t = 1000;
    CHECK_FALSE(step(pad, true, true, t, kHold + 1000));

    // Releasing the button does not rescue it: the pad has to be let go of first.
    CHECK_FALSE(step(pad, true, false, t, kHold + 1000));
    CHECK_FALSE(step(pad, false, false, t, 100));
    CHECK(step(pad, true, false, t, kHold + 100));
}

TEST_CASE("pad hold: the button joining mid-hold abandons it") {
    ui::PadHold pad;
    uint32_t t = 1000;
    CHECK_FALSE(step(pad, true, false, t, kHold - 200));
    CHECK_FALSE(step(pad, true, true, t, 1000));
    CHECK_FALSE(pad.holding());
}
