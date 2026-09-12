// The pad the pilot navigates with: a tap pages, a long touch goes home.
#include "doctest/doctest.h"
#include "ui/input/pad.h"

using namespace skyblip;

namespace {

constexpr uint32_t kHold = ui::Pad::kHoldMs;

ui::PadEvent step(ui::Pad& pad, bool pad_down, bool button_down, uint32_t& t, uint32_t ms) {
    ui::PadEvent seen = ui::PadEvent::None;
    for (uint32_t elapsed = 0; elapsed <= ms; elapsed += 10) {
        const ui::PadEvent event = pad.update(pad_down, button_down, t + elapsed);
        if (event != ui::PadEvent::None) seen = event;
    }
    t += ms;
    return seen;
}

}  // namespace

TEST_CASE("pad: a touch pages on the release, not on the contact") {
    ui::Pad pad;
    uint32_t t = 1000;
    CHECK(step(pad, true, false, t, 200) == ui::PadEvent::None);
    CHECK(step(pad, false, false, t, 100) == ui::PadEvent::Tap);

    // One touch, one page: the pad up says nothing more.
    CHECK(step(pad, false, false, t, 5000) == ui::PadEvent::None);
}

TEST_CASE("pad: the touch held past the hold goes home while the finger is still on it") {
    ui::Pad pad;
    uint32_t t = 1000;
    CHECK(step(pad, true, false, t, kHold - 100) == ui::PadEvent::None);
    CHECK(step(pad, true, false, t, 200) == ui::PadEvent::Hold);

    // Held on, and then let go of: neither fires again, and the release is not a page.
    CHECK(step(pad, true, false, t, 5000) == ui::PadEvent::None);
    CHECK(step(pad, false, false, t, 100) == ui::PadEvent::None);
}

// A late service step read the pad up, called it a page, and stole the way home.
TEST_CASE("pad: a release nobody polled through is still the touch it was") {
    ui::Pad pad;
    uint32_t t = 1000;
    pad.update(true, false, t);
    pad.update(true, false, t + ui::Pad::kSettleMs);
    CHECK(pad.update(false, false, t + kHold + 500) == ui::PadEvent::None);
    CHECK(pad.update(false, false, t + kHold + 600) == ui::PadEvent::Hold);
}

TEST_CASE("pad: a flutter shorter than the settle is not a touch") {
    ui::Pad pad;
    uint32_t t = 1000;
    CHECK(pad.update(true, false, t) == ui::PadEvent::None);
    CHECK(pad.update(false, false, t + ui::Pad::kSettleMs - 10) == ui::PadEvent::None);
    CHECK(step(pad, false, false, t, 500) == ui::PadEvent::None);
    CHECK_FALSE(pad.touching());
}

// A touch with the button down is the stow (core/power/shutdown.h), never a page.
TEST_CASE("pad: a touch the button joins says nothing, in either direction") {
    ui::Pad pad;
    uint32_t t = 1000;
    CHECK(step(pad, true, true, t, kHold + 1000) == ui::PadEvent::None);
    CHECK(step(pad, false, true, t, 100) == ui::PadEvent::None);

    ui::Pad joined;
    t = 1000;
    CHECK(step(joined, true, false, t, kHold - 200) == ui::PadEvent::None);
    CHECK(step(joined, true, true, t, 1000) == ui::PadEvent::None);
    CHECK(step(joined, false, false, t, 100) == ui::PadEvent::None);

    // Releasing the button does not rescue that touch: the pad has to be let go of first.
    CHECK(step(joined, true, false, t, kHold + 100) == ui::PadEvent::Hold);
}
