// What the panel's limits do to the refresh policy: held hot, stopped on a dying rail, parked.
#include "test/support/screen_rig.h"

// Glass rated 0..50 C; the Go's soak case is 72.4 C, project/research/enclosure-and-mount-go.md.
TEST_CASE("screen policy: a panel too hot to drive is left unrefreshed, with its rails down") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.churn(t, 20);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

// skyBlip stays on for the whole flight, so nothing but the pilot's switch may sleep the panel.
TEST_CASE("screen policy: a screen nobody changes is left awake, unrefreshed, rails down") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;
    const int sleeps = rig.chip.deep_sleeps;

    rig.run_seconds(t, 600);
    CHECK(rig.chip.present_count == before);
    CHECK(rig.chip.deep_sleeps == sleeps);
    CHECK(rig.chip.powered);
    CHECK_FALSE(rig.chip.rails_on);
}

// The ink migrates under bias, so a busy sky must not hold the rails up for a whole flight.
TEST_CASE("screen policy: a screen changing every second still rests between refreshes") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.alarm(2);
    rig.churn(t, 120);
    rig.run_seconds(t, 2);

    CHECK(rig.chip.present_count > 100);
    CHECK_FALSE(rig.chip.rails_on);
    CHECK(rig.chip.deep_sleeps == 0);
}

TEST_CASE("screen policy: nothing refreshes a panel held hot, partial or full") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.churn(t, 2);
    rig.run_seconds(t, 1);
    const int before = rig.chip.present_count;

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.run_seconds(t, 600);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: no refresh starts on a dying rail, partial or full") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.churn(t, 2);
    const int before = rig.chip.present_count;

    rig.state.supply_warned = true;
    rig.run_seconds(t, 600);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: the hold threshold is the rated limit, not a margin somebody chose") {
    CHECK(go::ScreenService::kHoldAboveDeciCelsius == 500);
}

// The rated limit is a temperature the panel works at, not the first one it refuses.
TEST_CASE("screen policy: the panel still refreshes at exactly its rated limit") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius);
    const int before = rig.chip.present_count;
    rig.churn(t, 3);
    CHECK(rig.chip.present_count > before);

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    const int held = rig.chip.present_count;
    rig.churn(t, 3);
    CHECK(rig.chip.present_count == held);
}

// A refresh already in flight when the panel goes out of range is left to finish.
TEST_CASE("screen policy: going hot mid-refresh does not abandon the frame on the glass") {
    Rig rig;
    uint32_t t = 0;
    rig.tick(t += 1000);
    REQUIRE(rig.chip.present_count == 1);

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.tick(t += 100);
    rig.tick(t += 2000);
    CHECK(rig.chip.present_count == 1);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: the traffic picture comes back once the panel has cooled") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.churn(t, 5);
    const int held = rig.chip.present_count;

    rig.die_temperature(300);
    rig.churn(t, 3);
    CHECK(rig.chip.present_count > held);
}

// The partial LUT may well ghost cold, but a full refresh a frame wears the glass out first (#62).
TEST_CASE("screen policy: below freezing every refresh is still a partial one") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.die_temperature(-50);
    for (int i = 0; i < 6; i++) {
        rig.churn(t, 1);
        rig.run_seconds(t, 2);
        CHECK_FALSE(rig.chip.last_full);
    }
}

TEST_CASE("screen policy: a cold panel with a static frame is still never re-presented") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.die_temperature(-100);
    rig.run_seconds(t, 60);
    CHECK(rig.chip.present_count == before);
}

TEST_CASE("screen policy: a board whose die sensor never read refreshes as it always did") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    REQUIRE_FALSE(rig.state.die_temperature_valid);

    rig.churn(t, 3);
    CHECK_FALSE(rig.chip.last_full);
    CHECK(rig.chip.powered);
}

TEST_CASE("screen policy: nothing routine is refreshed once the cell is at its cutoff") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.power_level = power::PowerLevel::Cutoff;
    rig.churn(t, 10);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: a low cell still gets its traffic picture") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.power_level = power::PowerLevel::Low;
    rig.churn(t, 3);
    CHECK(rig.chip.present_count > before);
}

TEST_CASE("screen policy: the white field the glass wears while off is drawn at the cutoff") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.power_level = power::PowerLevel::Cutoff;
    rig.screen.set_power(false);
    rig.run_seconds(t, 6);
    CHECK(rig.chip.present_count == before + 1);
    CHECK(rig.chip.last_full);
    CHECK_FALSE(rig.chip.powered);
}

// A command sent over a live BUSY is lost, and the park frame runs for seconds after it is issued.
TEST_CASE("screen policy: the panel sleeps when the park frame has finished, not when it starts") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 5);

    rig.screen.set_power(false);
    rig.tick(t += 100);
    CHECK(rig.chip.powered);
    CHECK(rig.epd.refreshing());

    rig.tick(t += 1000);
    CHECK(rig.chip.powered);

    rig.tick(t += 2000);
    CHECK_FALSE(rig.epd.refreshing());
    CHECK_FALSE(rig.chip.powered);
}

// The ink the sun develops: a park frame written over a waveform that was still driving the glass.
TEST_CASE("screen policy: a park mid-refresh waits for the glass, it does not talk over it") {
    Rig rig;
    uint32_t t = 0;
    rig.churn(t, 5);
    rig.state.own.sats = 7;
    rig.tick(t += 1000);
    const int before = rig.chip.present_count;
    REQUIRE(rig.epd.refreshing());

    rig.screen.set_power(false);
    rig.run_seconds(t, 6);
    CHECK(rig.chip.commands_while_busy == 0);
    CHECK(rig.chip.present_count == before + 1);
    CHECK(rig.chip.last_full);
    CHECK_FALSE(rig.chip.powered);
}

// POFCON fires milliseconds before the brownout reset, so the park frame cannot finish.
TEST_CASE("screen policy: a supply warning parks the panel without a park frame") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 5);
    const int before = rig.chip.present_count;

    rig.state.supply_warned = true;
    rig.screen.set_power(false);
    rig.tick(t += 100);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.powered);
}

TEST_CASE("screen policy: a supply warning stops the routine refreshes too") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.supply_warned = true;
    rig.churn(t, 10);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

// A held panel presents nothing, so nothing a pilot has not read reaches the glass.
TEST_CASE("screen policy: a held panel puts no new frame on the glass, dirty or not") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.die_temperature(go::ScreenService::kHoldAboveDeciCelsius + 1);
    rig.run_seconds(t, 5);
    const int held = rig.chip.present_count;

    rig.screen.mark_dirty();
    rig.screen.next_page();
    rig.run_seconds(t, 5);
    CHECK(rig.chip.present_count == held);
}
