// F5. The receiver's first solution: the one thing a pilot switching the device
// on wants confirmed, and the one moment its output is least worth
// transmitting. Two claims live here and they are separate: the confirmation
// fires once, and the settling period is longer the first time than after a
// receiver blinked.
#include "core/gnss/first_fix.h"
#include "doctest/doctest.h"

using namespace skyblip::gnss;

TEST_CASE("first fix: the confirmation fires once, on the first solution ever") {
    FirstFix f;
    CHECK_FALSE(f.ever_fixed());
    CHECK_FALSE(f.take_acquired());

    f.update(false, 1000);
    CHECK_FALSE(f.take_acquired());

    f.update(true, 5000);
    CHECK(f.ever_fixed());
    CHECK(f.take_acquired());
    CHECK_FALSE(f.take_acquired());  // consumed: nothing can chirp twice

    // A fix held over many ticks is still one acquisition.
    f.update(true, 6000);
    f.update(true, 7000);
    CHECK_FALSE(f.take_acquired());

    // And so is getting it back after losing it: the device already told the
    // pilot it can see the sky.
    f.update(false, 8000);
    f.update(true, 9000);
    CHECK_FALSE(f.take_acquired());
}

TEST_CASE("first fix: nothing is settled until the fix has been held") {
    FirstFix f;
    CHECK_FALSE(f.settled(0));

    f.update(true, 1000);
    CHECK(f.fix_since_ms() == 1000u);
    CHECK_FALSE(f.settled(1000));
    CHECK_FALSE(f.settled(1000 + kFirstFixSettleMs - 1));
    CHECK(f.settled(1000 + kFirstFixSettleMs));
    CHECK(f.settled(1000 + kFirstFixSettleMs + 60000));
}

TEST_CASE("first fix: losing the fix unsettles it, and the way back is shorter") {
    FirstFix f;
    f.update(true, 1000);
    REQUIRE(f.settled(1000 + kFirstFixSettleMs));

    // A receiver that blinked has nothing settled to offer while it is out.
    f.update(false, 40000);
    CHECK_FALSE(f.settled(40000));
    CHECK_FALSE(f.settled(90000));

    // Back with a fix: the almanac and the filters are warm, so the second wait
    // is the short one, not the cold-start one.
    f.update(true, 50000);
    CHECK_FALSE(f.settled(50000 + kRefixSettleMs - 1));
    CHECK(f.settled(50000 + kRefixSettleMs));
    CHECK(kRefixSettleMs < kFirstFixSettleMs);
}

TEST_CASE("first fix: the settling clock survives a millisecond counter that wraps") {
    FirstFix f;
    const uint32_t near_wrap = 0xFFFFFF00u;
    f.update(true, near_wrap);
    CHECK_FALSE(f.settled(near_wrap + 1000));
    CHECK(f.settled(near_wrap + kFirstFixSettleMs));  // wraps through zero
}
