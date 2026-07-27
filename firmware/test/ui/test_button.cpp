// Debounce tests for ui/input/button.h. What these pin down is the property the
// page cycling depends on: ONE event per press, however dirty the contact is.
#include "doctest/doctest.h"
#include "ui/input/button.h"

using namespace skyblip;

TEST_CASE("button: a stable press emits exactly one event") {
    ui::Button b;
    uint32_t t = 0;
    CHECK_FALSE(b.update(false, t));

    // Press, then hold well past the debounce window.
    CHECK_FALSE(b.update(true, t));  // first sample only starts the timer
    int events = 0;
    for (t = 10; t <= 500; t += 10)
        if (b.update(true, t)) events++;
    CHECK(events == 1);
    CHECK(b.down());
}

TEST_CASE("button: contact bounce does not produce extra events") {
    ui::Button b;
    uint32_t t = 0;
    // 5 ms of chatter, well inside kDebounceMs, then a settled press.
    const bool chatter[] = {true, false, true, false, true};
    int events = 0;
    for (bool level : chatter) {
        if (b.update(level, t)) events++;
        t += 1;
    }
    CHECK(events == 0);  // nothing was stable long enough to count
    for (; t <= 200; t += 5)
        if (b.update(true, t)) events++;
    CHECK(events == 1);
}

TEST_CASE("button: release then press again is a second event") {
    ui::Button b;
    uint32_t t = 0;
    b.update(true, t);
    for (t = 10; t <= 100; t += 10) b.update(true, t);
    REQUIRE(b.down());

    int events = 0;
    for (; t <= 300; t += 10)
        if (b.update(false, t)) events++;
    CHECK(events == 0);  // a release is never reported as a press
    CHECK_FALSE(b.down());

    for (; t <= 600; t += 10)
        if (b.update(true, t)) events++;
    CHECK(events == 1);
}

TEST_CASE("button: a glitch shorter than the window is ignored entirely") {
    ui::Button b;
    uint32_t t = 0;
    b.update(false, t);
    CHECK_FALSE(b.update(true, 100));   // spike starts
    CHECK_FALSE(b.update(false, 105));  // gone after 5 ms
    int events = 0;
    for (t = 110; t <= 400; t += 10)
        if (b.update(false, t)) events++;
    CHECK(events == 0);
    CHECK_FALSE(b.down());
}
