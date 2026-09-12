// The refresh policy over the real SSD1681 driver: partials, a wash an hour, swaps through black.
#include "test/support/screen_rig.h"

TEST_CASE("screen policy: a static frame is never re-presented") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    CHECK(rig.chip.present_count == 1);  // the boot frame, full
    CHECK(rig.chip.last_full);

    rig.run_seconds(t, 30);  // nothing on screen changes
    CHECK(rig.chip.present_count == 1);
}

TEST_CASE("screen policy: a minute of changing frames costs partials and no wash") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);  // boot full

    rig.churn(t, 60);
    CHECK(rig.chip.present_count > 1);
    CHECK_FALSE(rig.chip.last_full);
    CHECK(rig.screen.fasts_since_full() >= 60);
}

// SoftRF runs this glass on partials alone: the hourly wash is a vendor rule, not ghosting we saw.
TEST_CASE("screen policy: an hour of partials is settled by one wash") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.churn_at(t, 60000, go::ScreenService::kFullEveryMs / 60000 - 1);
    CHECK_FALSE(rig.chip.last_full);
    CHECK(rig.screen.fasts_since_full() > 0);

    rig.churn_at(t, 60000, 1);
    CHECK(rig.chip.last_full);
    CHECK(rig.screen.fasts_since_full() == 0);
}

TEST_CASE("screen policy: the wash waits out an alarm however long it stands") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);  // boot full

    rig.alarm(2);
    rig.churn_at(t, 60000, go::ScreenService::kFullEveryMs / 60000 + 10);
    CHECK_FALSE(rig.chip.last_full);

    rig.alarm(0);
    rig.churn(t, 1);
    CHECK(rig.chip.last_full);
}

TEST_CASE("screen policy: a page change goes through black, not through the full waveform") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.screen.next_page();
    rig.screen.tick(t += 1000);
    CHECK(rig.chip.present_count == before + 1);
    CHECK_FALSE(rig.chip.last_full);
    CHECK(rig.glass_all_black());

    // The page behind it does not wait out the one-a-second floor.
    rig.screen.tick(t += 400);
    CHECK(rig.chip.present_count == before + 2);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);
}

TEST_CASE("screen policy: a page change under an alarm goes straight to the picture") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.alarm(2);
    rig.screen.next_page();
    rig.screen.tick(t += 1000);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);
}

// The settings mode keeps the button to itself, so it has to give it back unasked.
TEST_CASE("screen policy: converging traffic takes the settings mode back off the glass") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.bus.input.push(messages::ButtonEvent{messages::kPadHeld});
    rig.run_seconds(t, 2);
    REQUIRE(rig.screen.mode() == go::Mode::Settings);
    REQUIRE(rig.screen.editor().active());

    // An advisory is not worth taking a pilot's page away.
    rig.alarm(1);
    rig.run_seconds(t, 2);
    CHECK(rig.screen.mode() == go::Mode::Settings);

    // A bearing worth turning the head for is. The menu goes, the traffic
    // picture comes back, and no wash flashes while the alarm stands.
    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(t, 2);
    CHECK(rig.screen.mode() == go::Mode::Traffic);
    CHECK(rig.screen.page() == go::Page::Radar);
    CHECK_FALSE(rig.screen.editor().active());
    CHECK_FALSE(rig.chip.last_full);
}

TEST_CASE("screen policy: presents wait for the panel, none is issued mid-refresh") {
    Rig rig;
    uint32_t t = 0;
    rig.screen.tick(t += 1000);  // boot full: panel busy ~1.5 s
    const int count = rig.chip.present_count;

    rig.state.clock.utc_valid = true;  // a visible change, right away
    rig.screen.mark_dirty();
    rig.screen.tick(t += 100);  // 1.1 s: full not settled yet
    CHECK(rig.chip.present_count == count);

    rig.screen.tick(t += 1500);  // settled: the pending change lands
    CHECK(rig.chip.present_count == count + 1);
}
