// ui/input, on its own. The debounce cases pin the property page cycling
// depends on: ONE event per press, however dirty the contact is. The gesture
// cases pin the property the companion link's security depends on: this product
// ships with BLE pairing off, so the double press IS the pairing, and a press
// meant for anything else must never be spendable as one.
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "ui/input/button.h"
#include "ui/input/gesture.h"

using namespace skyblip;

TEST_CASE("button: a press is reported when the thumb comes off, not when it lands") {
    ui::Button b;
    uint32_t t = 0;
    CHECK_FALSE(b.update(false, t));

    int events = 0;
    for (t = 0; t <= 500; t += 10)
        if (b.update(true, t)) events++;
    CHECK(events == 0);
    CHECK(b.down());

    for (; t <= 700; t += 10)
        if (b.update(false, t)) events++;
    CHECK(events == 1);
    CHECK_FALSE(b.down());
}

// A flashed unit paged under the thumb, two seconds before the rails went.
TEST_CASE("button: a press held into the power-off hold pages nothing, then or on release") {
    ui::Button b;
    uint32_t t = 0;
    int events = 0;
    for (; t <= power::kLongPressMs + 500; t += 10)
        if (b.update(true, t)) events++;
    CHECK(events == 0);

    for (; t <= power::kLongPressMs + 1000; t += 10)
        if (b.update(false, t)) events++;
    CHECK(events == 0);

    ui::Button quick;
    t = 0;
    events = 0;
    for (; t < power::kLongPressMs - 100; t += 10)
        if (quick.update(true, t)) events++;
    for (; t <= power::kLongPressMs + 200; t += 10)
        if (quick.update(false, t)) events++;
    CHECK(events == 1);  // just short of the hold is still a page press
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
    CHECK(events == 0);
    for (; t <= 400; t += 5)
        if (b.update(false, t)) events++;
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
    CHECK(events == 1);
    CHECK_FALSE(b.down());

    for (; t <= 600; t += 10)
        if (b.update(true, t)) events++;
    CHECK(events == 1);  // the second press has not been let go of yet
    for (; t <= 900; t += 10)
        if (b.update(false, t)) events++;
    CHECK(events == 2);
}

TEST_CASE("gesture: two presses inside the window authorise, one press refuses") {
    ui::ConfirmGesture g;
    g.arm(1000);
    CHECK(g.press(1100) == ui::Gesture::None);  // one press decides nothing yet
    CHECK(g.press(1250) == ui::Gesture::Confirm);
    CHECK_FALSE(g.armed());  // spent: the same pair cannot confirm twice
    CHECK(g.press(1300) == ui::Gesture::None);

    // The page press, made at a prompt. It is not ignored - it refuses.
    ui::ConfirmGesture lone;
    lone.arm(1000);
    CHECK(lone.press(1100) == ui::Gesture::None);
    CHECK(lone.tick(1100 + ui::ConfirmGesture::kDoublePressMs - 1) == ui::Gesture::None);
    CHECK(lone.tick(1100 + ui::ConfirmGesture::kDoublePressMs) == ui::Gesture::Cancel);
    CHECK_FALSE(lone.armed());
}

// The case that matters. A pilot cycling pages produces exactly this and it must
// not add up to a firmware upload: a press before the prompt existed is not part
// of the gesture, and two presses further apart than the window are two presses.
TEST_CASE("gesture: presses made before the prompt, and slow presses, authorise nothing") {
    ui::ConfirmGesture g;
    CHECK(g.press(500) == ui::Gesture::None);  // disarmed: paging, not answering
    CHECK(g.press(600) == ui::Gesture::None);
    CHECK(g.tick(2000) == ui::Gesture::None);

    // The prompt arrives between the two halves of a page double-tap. The half
    // that came first belongs to the pages, and cannot be counted here.
    g.arm(650);
    CHECK(g.press(700) == ui::Gesture::None);
    CHECK(g.tick(700 + ui::ConfirmGesture::kDoublePressMs) == ui::Gesture::Cancel);

    ui::ConfirmGesture slow;
    slow.arm(0);
    CHECK(slow.press(100) == ui::Gesture::None);
    CHECK(slow.press(100 + ui::ConfirmGesture::kDoublePressMs + 1) == ui::Gesture::None);
}

// The three things one button says have to stay disjoint, or the pilot has two
// of them at once: paging is one press, authorising is two, powering off is a
// hold that produces a single edge and then no more.
TEST_CASE("gesture: the authorising window sits between a page press and the power-off hold") {
    CHECK(ui::ConfirmGesture::kDoublePressMs > ui::Button::kDebounceMs);
    CHECK(ui::ConfirmGesture::kDoublePressMs < power::kLongPressMs);

    // A hold produces no press at all, so it can neither page nor answer a prompt.
    ui::Button b;
    ui::ConfirmGesture g;
    g.arm(0);
    int edges = 0;
    uint32_t t = 0;
    for (; t <= power::kLongPressMs + 200; t += 10)
        if (b.update(true, t)) edges++;
    for (; t <= power::kLongPressMs + 500; t += 10)
        if (b.update(false, t)) edges++;
    CHECK(edges == 0);
    CHECK(g.tick(power::kLongPressMs) == ui::Gesture::None);
    CHECK(g.armed());
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
